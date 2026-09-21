#include <opus.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
int main(void) {
 for(int channels=1;channels<=2;channels++) {
  for(int bitrate=24000;bitrate<=48000;bitrate+=24000) {
   int err=0; OpusEncoder* enc=opus_encoder_create(48000,channels,OPUS_APPLICATION_AUDIO,&err);
   if(!enc||err)return 2;
   opus_encoder_ctl(enc,OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
   opus_encoder_ctl(enc,OPUS_SET_BANDWIDTH(OPUS_BANDWIDTH_MEDIUMBAND));
   opus_encoder_ctl(enc,OPUS_SET_BITRATE(bitrate));
   opus_encoder_ctl(enc,OPUS_SET_VBR(1));
   opus_encoder_ctl(enc,OPUS_SET_VBR_CONSTRAINT(1));
   opus_encoder_ctl(enc,OPUS_SET_COMPLEXITY(10));
   opus_encoder_ctl(enc,OPUS_SET_INBAND_FEC(0));
   opus_encoder_ctl(enc,OPUS_SET_DTX(0));
   float pcm[1920*2]; unsigned char packet[1276]; long bytes=0; int lastbw=0,lastch=0,minch=2,maxch=0; clock_t start=clock();
   for(int frame=0;frame<250;frame++) {
    for(int i=0;i<1920;i++) for(int c=0;c<channels;c++) pcm[i*channels+c]=0.2f*sinf((float)(2*3.141592653589793*(c?1700:700)*(frame*1920+i)/48000));
    int n=opus_encode_float(enc,pcm,1920,packet,sizeof(packet)); if(n<0)return 3;
    bytes+=n;lastbw=opus_packet_get_bandwidth(packet);lastch=opus_packet_get_nb_channels(packet);if(lastch<minch)minch=lastch;if(lastch>maxch)maxch=lastch;
   }
   printf("input_channels=%d bitrate=%d output_channels=%d..%d bandwidth=%d payload_bps=%.0f encode_cpu_ms=%.1f audio_seconds=10\n",channels,bitrate,minch,maxch,lastbw,bytes*8/10.0,1000.0*(clock()-start)/CLOCKS_PER_SEC);
   opus_encoder_destroy(enc);
  }
 }
}
