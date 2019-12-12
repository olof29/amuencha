/* 
  Analyseur de MUsique et ENtraînement au CHAnt

  This file is released under either of the two licenses below, your choice:
  - LGPL v2.1 or later, https://www.gnu.org
    The GNU Lesser General Public Licence, version 2.1 or,
    at your option, any later version.
  - CeCILL-C, http://www.cecill.info
    The CeCILL-C license is more adapted to the French laws,
    but can be converted to the GNU LGPL.
  
  You can use, modify and/or redistribute the software under the terms of any
  of these licences, which should have been provided to you together with this
  sofware. If that is not the case, you can find a copy of the licences on
  the indicated web sites.
  
  By Nicolas . Brodu @ Inria . fr
  
  See http://nicolas.brodu.net/programmation/amuencha/ for more information
*/

#include <iostream>
#include <vector>
extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
}
using namespace std;

int main(int argc, char** argv) {
    
    avdevice_register_all();
    av_register_all();
    
    AVFormatContext * input_context = 0;
     
    AVInputFormat *ifmt = 0;
    //while ((ifmt = av_iformat_next(ifmt))) {
    while ((ifmt = av_input_audio_device_next(ifmt))) {
        //if (!ifmt || !ifmt->priv_class || !ifmt->priv_class->category) continue;
        //if (AV_IS_INPUT_DEVICE(ifmt->priv_class->category)) {
        //}
        if (!strcmp(ifmt->name, "alsa")) continue;
        /*AVDeviceInfoList *device_list = (AVDeviceInfoList*)av_mallocz(sizeof(AVDeviceInfoList));
        AVFormatContext * context = avformat_alloc_context();
        context->iformat = ifmt;
        context->priv_data = 0;
        //avdevice_list_devices(context, &device_list);
        ifmt->get_device_list(context, device_list);*/
        AVDeviceInfoList *device_list = 0;
        if (avdevice_list_input_sources(ifmt, ifmt->name, 0, &device_list)<0) continue;
        if (device_list->nb_devices==0) continue;
        cout << ifmt->name << ": " << ifmt->long_name << endl;
        for (int i=0; i<device_list->nb_devices; ++i) {
            if (!device_list->devices[i]) continue;
            cout << "  - " << device_list->devices[i]->device_name << ": " << device_list->devices[i]->device_description << endl;
        }
        
        AVDictionary* options = 0;
        int ret = avformat_open_input(&input_context, "jack_in",  ifmt, &options);
        cout << "avformat_open_input: " << ret << endl;
        break;
    }
     
    AVFormatContext * output_context = 0;
    
    AVOutputFormat *ofmt = 0;
    while ((ofmt = av_output_audio_device_next(ofmt))) {
        if (!strcmp(ofmt->name, "alsa")) continue;
        AVDeviceInfoList *device_list = 0;
        if (avdevice_list_output_sinks(ofmt, ofmt->name, 0, &device_list)<0) continue;
        if (device_list->nb_devices==0) continue;
        cout << ofmt->name << ": " << ofmt->long_name << endl;
        for (int i=0; i<device_list->nb_devices; ++i) {
            if (!device_list->devices[i]) continue;
            cout << "  - " << device_list->devices[i]->device_name << ": " << device_list->devices[i]->device_description << endl;
        }
        
        AVDictionary* options = 0;
        //int ret = avformat_alloc_output_context2(&output_context, ofmt, 0, "alsa_output.pci-0000_00_1f.3.analog-stereo");
        int ret = avformat_alloc_output_context2(&output_context, ofmt, 0, "jack_output");
        cout << "avformat_alloc_output_context2: " << ret << endl;
        break;
    }
    
    cout << "input_context->nb_streams = " << input_context->nb_streams << endl;
    
    vector<int> stream_mapping(input_context->nb_streams);
    for (int i=0, si=0; i<input_context->nb_streams; ++i) {
        AVStream *out_stream;
        AVStream *in_stream = input_context->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;

        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
            stream_mapping[i] = -1;
            continue;
        }

        stream_mapping[i] = si++;

        out_stream = avformat_new_stream(output_context, 0);
        if (!out_stream) {
            cout << "Failed allocating output stream" << endl;
            stream_mapping[i] = -1;
            continue;
        }

        if (avcodec_parameters_copy(out_stream->codecpar, in_codecpar)<0) {
            cout << "Failed to copy codec parameters" << endl;
            stream_mapping[i] = -1;
            continue;
        }
        out_stream->codecpar->codec_tag = 0;
    }
    
    cout << "output_context->nb_streams = " << output_context->nb_streams << endl;
    
    int ret = avformat_write_header(output_context, 0);
    cout << "avformat_write_header: " << ret << endl;
    
    AVPacket packet;
    while (true) {
        AVStream *in_stream, *out_stream;
        
        if (av_read_frame(input_context, &packet)<0) {
            cout << "could not read packet" << endl;
            break;
        }
        
        in_stream  = input_context->streams[packet.stream_index];
        if (packet.stream_index >= stream_mapping.size() || stream_mapping[packet.stream_index] < 0) {
            av_packet_unref(&packet);
            continue;
        }
        packet.stream_index = stream_mapping[packet.stream_index];
        out_stream = output_context->streams[packet.stream_index];
        
        /* copy packet */
        packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
        packet.pos = -1;
        
        if (av_interleaved_write_frame(output_context, &packet)<0) {
            cout << "Error while writing the packet" << endl;
        }
        
        av_packet_unref(&packet);
    }
    
    av_write_trailer(output_context);
    
    return 0;
}
