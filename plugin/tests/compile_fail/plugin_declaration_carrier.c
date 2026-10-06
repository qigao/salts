#include <salts/plugin_decl.h>
#define METHODS(X,I) X(I,R0,int,value,_)
CMETA_INTERFACE(expected_api,METHODS);
static int wrong_carrier;
#define EXPORTS(X) X(interface,(expected_api,&wrong_carrier),"bad","bad",1,0)
CMETA_PLUGIN_DECLARE(bad,"bad",(1,0,0),EXPORTS,CMETA_PLUGIN_PASSIVE());
