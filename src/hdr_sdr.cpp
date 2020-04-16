/* 
 * This file is part of the fast-hdr project (https://git.sofusrose.com/so-rose/fast-hdr).
 * Copyright (c) 2020 Sofus Rose.
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

// ThreadQ.h
#ifndef THREAD_Q
#define THREAD_Q

// Libraries
#include <queue>
#include <mutex>
#include <condition_variable>

using namespace std;

template <class T>
class ThreadQ {
	// A Thread Safe Queue for Threaded Messaging
	public:
		ThreadQ() :
			queue_unsafe(),
			queue_lock(),
			thread_msg() { }
		
		~ThreadQ() { }
		
		void push(T v) {
			// Push to Queue
			
			// Aquire Lock
			lock_guard<mutex> lock(queue_lock);
			
			// Push to Unsafe Queue
			queue_unsafe.push(v);
			
			// Notify One Waiting Thread to Continue
			//    ONLY ONE: Otherwise a Data Race Occurs.
			thread_msg.notify_one();
		}
		
		T pop() {
			// Pop from Queue
			
			// Aquire Lock
			unique_lock<mutex> lock(queue_lock);
			
			// Release Lock Until queue_unsafe Has Elements
			while(queue_unsafe.empty()) { thread_msg.wait(lock); }
			
			// Aquire Front Element & Pop It
			T v = queue_unsafe.front();
			queue_unsafe.pop();
			
			// Return the Element
			return v;
		}

	private:
		// Internal, Unsafe Queue
		queue<T> queue_unsafe;
		
		// Queue and Thread Messenger
		mutable mutex queue_lock;
		
		condition_variable thread_msg;
};
#endif

// Usage: <RGB PRODUCER> | ./hdr_sdr [WIDTH] [HEIGHT] | <RGB ENCODER>

// Libraries
#include <iostream>
#include <fstream>
#include <string>

#include <math.h>
#include <algorithm>

#include <thread>

#include <unistd.h> 
#include <sys/stat.h>



// User Defines
#define IMG_BITS 8

#define PAY_SIZE     1 // Images per (Processing) Payload
#define BUFFER_SIZE  16 // Max Payloads to Keep in Memory



// User Types
typedef uint8_t img_uint; // Must Hold Image Data Point of >= IMG_BITS Size

#define IMG_INT_MAX ((1 << IMG_BITS) - 1)
#define IMG_INT_MAX_D ( (double) IMG_INT_MAX )



// Resolution and Size of LUTD (Dimensioned LUT
#define LUTD_BITS IMG_BITS
#define LUTD_CHNLS 3

#define LUTD_RES (1 << LUTD_BITS) // 2**LUTD_BITS

#define LUTD_SIZE (LUTD_RES * LUTD_RES * LUTD_RES * LUTD_CHNLS)

// Each 8-Bit YUV Triplet => Corresponding YUV Triplet.
//    4D LUT, Three "Cubes": Y Cube, U Cube, V Cube.
//    0. To Advance One Y, U, V, C(hannel) vue, Advance by a Stride
//    1. Use Old YUV to Find X,Y,Z Index On Cube(s).
//    2. Compute New YUV by Indexing Each Cube Identically

#define LUTD_Y_STRIDE(y) (y << (0 * LUTD_BITS)) // Y: Shift by (2**LUTD_BITS)**0
#define LUTD_U_STRIDE(u) (u << (1 * LUTD_BITS)) // U: Shift by (2**LUTD_BITS)**1
#define LUTD_V_STRIDE(v) (v << (2 * LUTD_BITS)) // V: Shift by (2**LUTD_BITS)**2
#define LUTD_C_STRIDE(c) (c << (3 * LUTD_BITS)) // C: Shift by (2**LUTD_BITS)**3



// Namespacing
using namespace std;



//###########
// - LUT Methods
//###########

void read_lutd(img_uint *lutd, string path_lutd) {
	// The array must be sized as LUTD_SIZE.
		
	ifstream file_lutd(path_lutd, ifstream::binary);
		
	if (file_lutd.is_open()) {
		file_lutd.read(reinterpret_cast<char*>(lutd), LUTD_SIZE);
	}
}

void trans_lutd(
	img_uint *y,
	img_uint *u,
	img_uint *v,
	img_uint *lutd
) {
	// Returns YUV Transformed by LUT
	
	// Index the Flat LUTD Using Y,U,V Strides
	size_t ind_lutd = (
		LUTD_Y_STRIDE(*y) +
		LUTD_U_STRIDE(*u) +
		LUTD_V_STRIDE(*v)
	);
	
	*y = lutd[ind_lutd + LUTD_C_STRIDE(0)];
	*u = lutd[ind_lutd + LUTD_C_STRIDE(1)];
	*v = lutd[ind_lutd + LUTD_C_STRIDE(2)];
}



//###########
// - Processing Methods
//###########

void hdr_sdr(img_uint *pay, size_t size_pay, img_uint *lutd) {
	// Process the Payload Using Precomputed YUV Destinations
	
	for (size_t i_img = 0; i_img < PAY_SIZE; i_img++) {
		size_t size_img = size_pay / PAY_SIZE;
		size_t stride_img = size_img / 3;
	
		#pragma omp parallel for
		for (size_t i = i_img*size_img; i < i_img*size_img + stride_img; i++) {
			img_uint *y = &pay[i + 0*stride_img];
			img_uint *v = &pay[i + 1*stride_img];
			img_uint *u = &pay[i + 2*stride_img];
			
			trans_lutd(y, u, v, lutd);
		}
	}
}



//###########
// - Processing Loop
//###########

void read_stdin(
	ThreadQ<img_uint*> &queue_read,
	ThreadQ<img_uint*> &queue_proc,
	size_t size_pay
) {
	while (true) {
		// GET: An Unused Payload from START/WRITER.
		img_uint* pay = queue_read.pop();
		
		// DO: Read Payload from STDIN.
		cin.read(reinterpret_cast<char*>(pay), size_pay);
				
		// PUT: A Read Payload to MAIN.
		queue_proc.push(pay);
	}
}

void proc(
	ThreadQ<img_uint*> &queue_proc,
	ThreadQ<img_uint*> &queue_write,
	size_t size_pay,
	
	img_uint *lutd
) {
	while (true) {
		// GET: A Read Payload from READER.
		img_uint* pay = queue_proc.pop();
		
		// DO: Process the Payload!
		hdr_sdr(pay, size_pay, lutd);
		
		// PUT: A Processed Payload to WRITER.
		queue_write.push(pay);
	}
}

void write_stdout(
	ThreadQ<img_uint*> &queue_write,
	ThreadQ<img_uint*> &queue_read,
	size_t size_pay
) {
	while (true) {
		// GET: A Processed Payload from MAIN.
		img_uint* pay = queue_write.pop();
		
		// DO: Write Payload to STDOUT.
		cout.write(reinterpret_cast<char*>(pay), size_pay);
		
		// PUT: An Unused Payload to READER.
		queue_read.push(pay);
	}
}



//###########
// - Application
//###########

int main(int argc, char **argv) {
	
	// PARSE: (Width, Height) => Image Size, LUT Path from Command Line
	unsigned int x_res = 0; unsigned int y_res = 0; string path_lutd;
	if (argc == 4) {
		x_res     = stoi(argv[1]);
		y_res     = stoi(argv[2]);
		path_lutd = string(argv[3]);
	} else {
		cout << "Usage: ./hdr_sdr [WIDTH] [HEIGHT] [PATH_LUTD]" << endl;
		
		return 1;
	}
	
	// PAYLOAD: Allocate Payload Buffer
	size_t size_img = x_res * y_res * 3; // # Bytes per Image
	size_t size_pay = size_img * PAY_SIZE;          // # Bytes per Processing Payload
	
	img_uint* buf_pay = (img_uint*) malloc( sizeof(img_uint) * size_pay * BUFFER_SIZE );
	
	// LUTD: Allocate & Read LUTD
	img_uint *lutd = (img_uint*) malloc( sizeof(img_uint) * LUTD_SIZE );
	read_lutd(lutd, path_lutd);
	
	
	// QUEUES: Setup Threaded Payload Processing Loop
		// --> READER ----> PROC ----> WRITER --
		// THREADED  : The slowest component decides the payload throughput.
		// UNTHREADED: Each component slows the payload throughput.
	ThreadQ<img_uint*> queue_read  = ThreadQ<img_uint*>(); // Pointers to Read Payloads To
	ThreadQ<img_uint*> queue_proc  = ThreadQ<img_uint*>(); // Pointers to Process Payloads In
	ThreadQ<img_uint*> queue_write = ThreadQ<img_uint*>(); // Pointers to Write Payloads From
	
	// QUEUES: Mark All Payloads as Unused
	for (size_t i = 0; i < BUFFER_SIZE; i++) {
		queue_read.push(buf_pay + i*size_pay);
	}
	
	// THREADS: Start READER, PROC, and WRITER
	thread th1_reader(read_stdin  , ref(queue_read) , ref(queue_proc) , size_pay);
	thread th2_proc  (proc        , ref(queue_proc) , ref(queue_write), size_pay, lutd);
	thread th3_writer(write_stdout, ref(queue_write), ref(queue_read) , size_pay);
	 
	// THREADS: Wait for Writer to Finish
	th3_writer.join();
}
