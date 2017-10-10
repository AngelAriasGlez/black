#ifndef PLATFORM_H
#define PLATFORM_H

#include "../core/String.hpp"
#include <vector>
#include <map>
#include "../graphics/Font.hpp"


	class Platform {

	public:
		Platform();
		~Platform();

		static int start();
		static bool create();

		static void shutdown();


		static unsigned int getDisplayWidth();
		static unsigned int getDisplayHeight();

		static double getDisplayDensity();
		static double getDisplaySize();

		static String getCacheDir();
		static String getAssetsDir();
        static String getSystemMusicDir();

		static String displayFileDialog(const char* title, const char* filterDescription, const char* filterExtensions, const char* initialDirectory);


		class AudioInfo {
		public:
			std::vector <String> hosts;
			std::vector <String> devices;
			int defaultDevice;
			int currentDevice;

		};

		static AudioInfo getAudioInfo();
		static bool setAudioDevice(int index);
        
        static double getDisplayScale();
        
        static void openMediaSelector(int type = 0, int source = 0);


        static String createTempFile(String filename);
        
        static bool saveToVideos(String filename);
	};


#endif // 
