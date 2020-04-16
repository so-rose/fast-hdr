# Fast HDR-->SDR Conversion Without a GPU

A slightly insane solution to a devilishly irritating problem.



## Problem: What's the Problem?

I like to master my 3D work in HDR, because, well, I can. I'm a color nerd without a reference monitor.

When I want to show my friends, I try to stream this HDR content using the wonderful Jellyfin (Emby fork). Problems ensue: https://github.com/jellyfin/jellyfin/issues/415 . This isn't the first time somebody has wanted to do something like this, and had trouble: https://www.verizondigitalmedia.com/blog/best-practices-for-live-4k-video-ingestion-and-transcoding/ .

Long story short? The colors look **WRONG**. It's all wrong. It irritates me.

I am a normal person: I don't own an HDR screen, so like any normal person I spend days writing software to solve my irritations!

*Plus, my screen just doesn't do BT.2020. I'm not sure any screen really does.*



## Problem: Isn't This Solved in VLC/mpv/etc. ?

It is indeed! They do a very nice 2020/PQ --> 709 on the GPU, in real time, and with very nice tone mapping.

However, ffmpeg cannot do this in realtime. This means, no live transcoding --> streaming of HDR clips through Jellyfin - and none of the other fun realtime imaging one wants to do on a when one isn't plugged into a loud ASIC more power-hungry than my refrigerator.



## Problem: How to Solve It?

My approach is as follows:
* **Piping**: We keep `ffmpeg` to decode, but have it spit out raw YUV444 data, which we process and spit back out to `ffmpeg` for encoding.
* **Threaded I/O**: Read, Write, and Process happen in different threads - the slowest determines the throughput.
* **8-Bit LUT**: The actual imaging operations are first precomputed into a `256x256x256x3` LUT. These arbitrarily complex global operations are then applied with a simple lookup (no interpolation - just pure 8-bit madness!).



## Problem: So - Was it Worth It?

Well - all the optimization and reverse engineering aside, the colors in my HDR content looks right through Jellyfin. So? All is right with the world again!

Though, to be honest, none of my non-technical peers quite understand where I've been the past couple days...



# Features

How can `fast-hdr` make your day brighter?
* [**Hackable**] Want to easily implement arbitrarily complex global imaging operations, whilst seeing them executed with decent accuracy in real time? It's not like there's a lot of code to sift through - just spice up `hdr_sdr_gen.cpp` with your custom tonemapping function, or whatever you want!
* [**CPU-Based**] There's no GPU here, which means flexibility. Run it on your Raspberry Pi! Run it on a phone! Run it cheaply in - dare I say it - the cloud!
* [**Fast**] With FFMPEG as a decoder piped in, I get `30` FPS at 4K on a `Threadripper 1950X (16-core)`.
	* Of course, not all CPUs in the world are Threadrippers, so the `ffmpeg` wrapper sets the max resolution to `1080p`, which works like smooth butter; `80` FPS on my less-powerful-but-still-kinda-beefy server. Comment it out if you don't like it :P
* [**Jellyfin-oriented `ffmpeg` Wrapper**] Designed for Jellyfin, there's a Python wrapper around `ffmpeg` which injects the HDR-->SDR conversion into any complex `ffmpeg` invocation!
	* Everyone uses `ffmpeg` - therefore, `fast-hdr` can run everywhere :) !
* [**Realtime, Arbitrarily Complex Global Operations**] With a `3 * 256^3` sized 8-Bit LUT (just 50MB in memory!) precomputed at compile-time by `hdr_sdr_gen.cpp`, you can perform arbitrarily complex global operations, and apply them to an image (each 8-bit YUV triplet has a corresponding triplet) without any interpolation whatsoever. In this case, that's just an inverse PQ operator, followed by a global tonemap, followed by an sRGB correction.
* [**UNIX Philosophy**] `fast-hdr` does processing, and that's it. It's just a brick. You can use any decoder / encoder you want - as long as it spits out / gobbles up YUV444p data to `stdout`! Hell, I don't know, go crazy with OpenHEVC, or whatever shiny new thing ILM cooked up this time. (Though I'd probably use `ffmpeg` as it's less hard and at least tested).



# How To Do

0. Make sure you're on Linux, and have `python3` and `gcc` (and can invoke `g++`).
1. Get your hands on a standalone `ffmpeg` and `ffprobe`, and put it in `res`.
2. Run `compile.sh`. It might take a sec, it has to precompute the `.lutd`.

Then, let it play with some footage! For example, here's a complex ffmpeg invocation (run by Jellyfin):

```bash
rm -rf .tmp

FFMPEG="./dist/ffmpeg-custom"
FFMPEG_PY="./dist/ffmpeg"
HDR_SDR="./dist/hdr_sdr"
LUT_PATH="./dist/cnv.lut8"

# Test on a nice shot of a cute little catterpiller.
TIME="00:01:28"
FILE="./Life Untouched 4K Demo.mp4"
HLS_SEG=".tmp/transcodes/hash_of_your_hdr_file_no_need_to_replace%d.ts"
FILE_OUT=".tmp/transcodes/hash_of_your_hdr_file_no_need_to_replace.m3u8"

TEST=true "$FFMPEG_PY" -ss "$TIME" -f mp4 -i file:"$FILE" -map_metadata -1 -map_chapters -1 -threads 0 -map 0:0 -map 0:1 -map -0:s -codec:v:0 libx264 -pix_fmt yuv420p -preset veryfast -crf 23 -maxrate 34541128 -bufsize 69082256 -profile:v high -level 4.1 -x264opts:0 subme=0:me_range=4:rc_lookahead=10:me=dia:no_chroma_me:8x8dct=0:partitions=none  -force_key_frames:0 "expr:gte(t,0+n_forced*3)" -g 72 -keyint_min 72 -sc_threshold 0 -vf "scale=trunc(min(max(iw\,ih*dar)\,1920)/2)*2:trunc(ow/dar/2)*2" -start_at_zero -vsync -1 -codec:a:0 aac -strict experimental -ac 2 -ab 384000 -af "volume=2" -copyts -avoid_negative_ts disabled -f hls -max_delay 5000000 -hls_time 3 -individual_header_trailer 0 -hls_segment_type mpegts -start_number 0 -hls_segment_filename "$HLS_SEG" -hls_playlist_type vod -hls_list_size 0 -y "$FILE_OUT"
```



## How To: Jellyfin

To get Jellyfin to convert HDR footage when transcoding for web playback, follow the How To steps and make sure it works locally.

Then:
0. Make sure the python script `ffmpeg`, the actual binaries `ffmpeg-custom` and `ffprobe`, the compiled binary `hdr_sdr`, and the generated LUT `cnd_lut8` are in `dist`.
1. Copy `dist` to somewhere on your server owned by the `jellyfin` user. Probably a good idea to `chmod` it too.
2. In the Jellyfin interface, in Playback -> `ffmpeg` Path, point it at the Python script `ffmpeg`. *It's critical that `ffprobe` is there too, otherwise Jellyfin will be unable to read header info about your files, like audio tracks or subtitle tracks.*

Things to be aware of:
- Seeking is super slow, as for some reason the wrapper doesn't understand keyframes, and will try to encode its way to wherever you seeked to. So don't seek :) Resuming works fine, however.
- Occasionally, you might have to `killall ffmpeg-custom && killall hdr_sdr`, or they'll keep eating CPU cycles after you've stopped watching. I'm still not sure why. I have no idea why.



# Testing / Stability / Modularization / Any Kind of Good Software Development Practices

No



# Contributing

*Is that a `malloc` I see in your C++? In this devout Stroustrup'ian neighborhood? Blasphemy...*

I like you, you insane monkey - if this horrible hack truly is actually useful to you then I'm speechless!

I'm happy to help you make it work, help with bugs (make an Issue!), and/or (probably) accept any kind of PR. God knows there's enough wrong with this piece of software that it needs some fixing up...

Development happens at https://git.sofusrose.com/so-rose/fast-hdr .



# TODO

It gets wilder!
* **Seeking in Jellyfin**: One cannot seek from the Jellyfin interface. This may have something to do with the `ffmpeg` wrapper not catching the `q` to quit.
* **Better Profiling**: Measuring performance characteristics of a threaded application like this isn't super easy, but probably worth it.
* **Dithering**: There's a reason nobody precomputes transforms on every possible 8-bit YUV value: Posterization. We can solve this to a quite reasonable degree by dithering while applying the LUT!
* **More Robust `ffmpeg` Wrapper**: It's a little touchy right now. Like, it throws an exception if you give it a wrong `-f`...
* **More Vivid Image**: Personal preference, I like vivid! This is just a matter of tweaking the image processing pipeline.
* **Verify Color Science**: Right now, it's all done a bit by trial and error. It does, however, match VLC's output quite well.
* **10-bit LUT**: Who cares if this needs a 3GB buffer to compute? It solves posterization issues, and allows directly processing 10-bit footage to boot!
* **Gamut Rendering**: Currently, there's no gamut mapping or rendering intent management. It coule be nice to have.
* **Better Tonemapping**: The present tonemapping is arbitrarily chosen, even though it does look nice.
* **Variable Input**: YUV444p isn't nirvana. Plus, imagine how fast clever LUT'ing directly on 4K YUV420p data might be.
