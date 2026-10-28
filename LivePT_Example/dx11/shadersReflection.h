	#define shader(VariableName) VariableName,

	enum class vertex {
		#include "..\shaders\vsList.h"
	};
	enum class pixel {
		#include "..\shaders\psList.h"
	};
	

	#if EditMode

		#undef shader
		#define shader(VariableName) #VariableName ,
		const char* vsList[] = {
			#include "..\shaders\vsList.h"
		};
		const char* psList[] = {
			#include "..\shaders\psList.h"
		};

		#undef shader

	#endif