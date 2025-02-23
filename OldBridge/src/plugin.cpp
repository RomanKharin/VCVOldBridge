#include "plugin.hpp"


Plugin* pluginInstance;


void init(Plugin* p) {
	pluginInstance = p;

	// Add modules here
	p->addModel(modelSmile);
	p->addModel(modelLooper);
	p->addModel(modelFilter);
}
