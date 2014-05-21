一个命令行的MPEG TS发包工具，支持发送单播和组播，支持同时发到多个目标，支持添加RTP头，支持模拟丢包。

tclite [ts caster command line edition]
Usage
	-h --help:	Display this usage information.

	-i --input:	Set the input file.
	-d --dest:	Set destinations, such as -d 225.2.2.2:6000,172.16.6.99:12000.
	-l --loopfile:	-1-->always loop, 0-->no loop, 1-->loop times.
	--ttl:		Time to live value.
	--bitrate:	Force bitrate. if not set, use auto bitrate.

	--rtpheader:	Whether to add rtp header, 0-->not add, 1-->add.
			Can be set for each destination respectively, such as "--rtpheader 1, 0"
	--rtpssrc:	Can be set for each destination respectively
	--rtpseqstart:	Rtp sequence number will begin with rtpseqstart
			Can be set for each destination respectively

	--lostrate:	Can be set for each destination respectively
	--dropnum:	How many packets droped each time. can be set for each destination respectively

	--seekpos:	Start offset by byte. No effect on loop
	--seektime:	Start offset by second. NO effect on loop
	--startpos:	Start offset by byte.
	--endpos:	End offset by byte.
	--starttime:	Start offset by second.
	--endtime:	End offset by second.
	--speedscale:	Send faster or slower. For example, 100 original speed, 50 half speed, 200 double speed.
