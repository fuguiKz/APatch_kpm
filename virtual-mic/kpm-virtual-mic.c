#include <linux/module.h>
#include <linux/kernel.h>
// #include <linux/slab.h> // 移除
#include <linux/string.h>
// #include <linux/time.h> // 移除
#include <linux/delay.h>
// #include <sys/types.h> // 移除
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/asound.h>
#include <compiler.h>
#include <kpmodule.h>
#include <hook.h>

KPM_NAME("kpm-virtual-mic");
KPM_VERSION("1.0.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("your_name");
KPM_DESCRIPTION("Virtual Microphone Kernel Module");

#define AUDIO_DATA_SIZE 4096 // 根据实际情况调整
static char *custom_audio_data = NULL;
static int  custom_audio_data_len = 0;
static int  custom_audio_data_pos = 0;

static void *original_snd_pcm_set_ops = NULL;
typedef void (*snd_pcm_set_ops_func_t)(struct snd_pcm *pcm, int direction, const struct snd_pcm_ops *ops);
static struct snd_pcm_ops original_ops;


    static int my_trigger(struct snd_pcm_substream *substream, int cmd);
    static int my_mmap(struct snd_pcm_substream *substream, struct vm_area_struct *vma);
    static struct page *my_page(struct snd_pcm_substream *substream,  unsigned long offset);
    static int my_close(struct snd_pcm_substream *substream);
    static int my_open(struct snd_pcm_substream *substream);
   static struct snd_pcm_ops my_ops;
   static void *original_snd_pcm_lib_read = NULL;
   typedef snd_pcm_sframes_t (*snd_pcm_lib_read_func_t)(struct snd_pcm_substream *substream, void __user *buf, snd_pcm_uframes_t frames);


    void my_snd_pcm_set_ops(hook_fargs3_t *args, void *udata)
    {
        struct snd_pcm *pcm = (struct snd_pcm *) syscall_argn(args, 0);
        int direction = (int) syscall_argn(args, 1);
        const struct snd_pcm_ops *ops = (const struct snd_pcm_ops *) syscall_argn(args, 2);

        if (direction == SNDRV_PCM_STREAM_CAPTURE) { // Only replace capture stream
              my_ops = *ops;
              my_ops.trigger = my_trigger; // replace my trigger function
              my_ops.mmap = my_mmap; // replace my mmap function
              my_ops.page = my_page; //replace my page function
              my_ops.close = my_close; //replace my close function
              my_ops.open = my_open; //replace my open function
              syscall_argn(args, 2) = &my_ops; // replace origin ops with my_ops
            pr_info("Replaced snd_pcm_ops successfully for capture.\n");
       }
    }
       static int my_mmap(struct snd_pcm_substream *substream, struct vm_area_struct *vma)
       {
           if(original_ops.mmap)
              return original_ops.mmap(substream, vma);
           return 0;
       }
       static struct page *my_page(struct snd_pcm_substream *substream,
                   unsigned long offset)
       {
          if(original_ops.page)
             return original_ops.page(substream, offset);
         return snd_pcm_default_page_ops(substream, offset);
       }
       static int my_close(struct snd_pcm_substream *substream){
            if(original_ops.close)
              return original_ops.close(substream);
           return 0;
       }
        static int my_open(struct snd_pcm_substream *substream)
        {
            if(original_ops.open)
             return original_ops.open(substream);
           return 0;
        }


    static int my_trigger(struct snd_pcm_substream *substream, int cmd)
    {
        int ret = 0;
        struct snd_pcm_runtime *runtime = substream->runtime;
        if (cmd == SNDRV_PCM_TRIGGER_START) { // when start capture
            pr_info("Virtual mic started.\n");
        } else if (cmd == SNDRV_PCM_TRIGGER_STOP) { // when stop capture
            pr_info("Virtual mic stop.\n");
        }
        if(original_ops.trigger)
            ret = original_ops.trigger(substream, cmd);
        return ret;
    }

   static snd_pcm_sframes_t my_snd_pcm_lib_read(hook_fargs3_t *args, void *udata)
   {
      struct snd_pcm_substream *substream = (struct snd_pcm_substream *)syscall_argn(args, 0);
      void __user *buf = (void __user *)syscall_argn(args, 1);
      snd_pcm_uframes_t frames = (snd_pcm_uframes_t)syscall_argn(args, 2);

       struct snd_pcm_runtime *runtime = substream->runtime;

       if (!custom_audio_data || custom_audio_data_len == 0) {
         pr_err("No custom audio data loaded.\n");
         return 0;
       }

      if (custom_audio_data_pos >= custom_audio_data_len) {
          custom_audio_data_pos = 0;
      }

      int copy_size = min((int)frames * runtime->frame_bits / 8, custom_audio_data_len - custom_audio_data_pos);
      if (copy_size > 0) {
          if (copy_to_user(buf, custom_audio_data + custom_audio_data_pos, copy_size))
              return -EFAULT;
          custom_audio_data_pos += copy_size;
       }
      return frames;
   }

   static int my_virtual_mic_init(const char *args)
   {
	pr_info("kpm-virtual-mic init, args: %s\n", args);

    custom_audio_data = vmalloc(AUDIO_DATA_SIZE);
    if (!custom_audio_data) {
        pr_err("Failed to allocate memory for custom audio data.\n");
        return -ENOMEM;
    }
    memset(custom_audio_data, 0, AUDIO_DATA_SIZE);

   // 模拟一些自定义的音频数据，你可以从文件读取或动态生成数据
   // 例如，这里生成一个简单的正弦波
   for (int i = 0; i < AUDIO_DATA_SIZE; i++) {
      custom_audio_data[i] = (char)(127 * (sin(2 * 3.14159 * 440 * i / 44100)));
   }

   custom_audio_data_len = AUDIO_DATA_SIZE;
   custom_audio_data_pos = 0;


    original_snd_pcm_set_ops = (void *)kallsyms_lookup_name("snd_pcm_set_ops");
    if (!original_snd_pcm_set_ops) {
      pr_err("Failed to find snd_pcm_set_ops\n");
        vfree(custom_audio_data);
        return -EINVAL;
    }
    hook_err_t err = inline_hook_wrap((snd_pcm_set_ops_func_t)original_snd_pcm_set_ops, my_snd_pcm_set_ops, NULL);
    if (err){
      pr_err("Failed to hook snd_pcm_set_ops, error: %d\n", err);
        vfree(custom_audio_data);
        return err;
    }
    pr_info("Hooked snd_pcm_set_ops successfully.\n");


   original_snd_pcm_lib_read = (void *)kallsyms_lookup_name("snd_pcm_lib_read");
    if (!original_snd_pcm_lib_read) {
        pr_err("Failed to find snd_pcm_lib_read\n");
        vfree(custom_audio_data);
        return -EINVAL;
    }

   err = inline_hook_wrap((snd_pcm_lib_read_func_t)original_snd_pcm_lib_read, my_snd_pcm_lib_read, NULL);
    if (err){
        pr_err("Failed to hook snd_pcm_lib_read, error: %d\n", err);
       vfree(custom_audio_data);
        return err;
    }
   pr_info("Hooked snd_pcm_lib_read successfully.\n");
	return 0;
}


void my_virtual_mic_exit()
{
	pr_info("kpm-virtual-mic exit\n");
    if (original_snd_pcm_set_ops) {
        unhook(original_snd_pcm_set_ops);
        pr_info("Unhooked snd_pcm_set_ops successfully.\n");
    }
     if (original_snd_pcm_lib_read) {
        unhook(original_snd_pcm_lib_read);
        pr_info("Unhooked snd_pcm_lib_read successfully.\n");
    }
    if (custom_audio_data) {
        vfree(custom_audio_data);
        pr_info("Freed custom audio data.\n");
    }
}

KPM_INIT(my_virtual_mic_init);
KPM_EXIT(my_virtual_mic_exit);
