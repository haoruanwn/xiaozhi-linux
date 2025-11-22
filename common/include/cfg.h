#ifndef __CFG_H
#define __CFG_H

#define AUDIO_PORT_UP    5676   /* sound_app向control_center的这个端口上传音频 */
#define AUDIO_PORT_DOWN  5677   /* control_center向sound_app的这个端口下发音频 */
#define UI_PORT_UP    5678      /* GUI向control_center的这个端口上传UI信息 */
#define UI_PORT_DOWN  5679      /* control_center向GUI的这个端口下发UI信息 */

#define NET_BRIDGE_PORT_IN  8000   /* control_center listens on this port for network messages */
#define NET_BRIDGE_PORT_OUT 8001   /* net_bridge listens on this port for outgoing messages */

#define CFG_FILE "/etc/xiaozhi.cfg"

#endif
