#include "ofxThreadedImageLoaderSingleton.h"
#include <sstream>


ofxThreadedImageLoaderSingleton* ofxThreadedImageLoaderSingleton::__instance = 0;

//--------------------------------------------------------------
ofxThreadedImageLoaderSingleton* ofxThreadedImageLoaderSingleton::instance(){
  if (__instance == 0){
    __instance = new ofxThreadedImageLoaderSingleton();
  }
  return __instance;
}

//--------------------------------------------------------------
void ofxThreadedImageLoaderSingleton::setup(){
  instance();
}

//--------------------------------------------------------------
ofxThreadedImageLoaderSingleton::ofxThreadedImageLoaderSingleton() 
:ofThread()
{
	nextID = 0;
    ofAddListener(ofEvents().update, this, &ofxThreadedImageLoaderSingleton::update);
	ofAddListener(ofURLResponseEvent(),this,&ofxThreadedImageLoaderSingleton::urlResponse);
    
    startThread();
    lastUpdate = 0;
}

ofxThreadedImageLoaderSingleton::~ofxThreadedImageLoaderSingleton(){
	condition.signal();
    ofRemoveListener(ofEvents().update, this, &ofxThreadedImageLoaderSingleton::update);
	ofRemoveListener(ofURLResponseEvent(),this,&ofxThreadedImageLoaderSingleton::urlResponse);
}

// Load an image from disk.
//--------------------------------------------------------------
void ofxThreadedImageLoaderSingleton::loadFromDisk(ofImage& image, string filename) {
  instance();
	__instance->nextID++;
	ofImageLoaderEntry entry(image, OF_LOAD_FROM_DISK);
	entry.filename = filename;
	entry.id = __instance->nextID;
	entry.image->setUseTexture(false);
	entry.name = filename;
    
    __instance->lock();
    __instance->images_to_load_buffer.push_back(entry);
    __instance->condition.signal();
    __instance->unlock();
}


// Load an url asynchronously from an url.
//--------------------------------------------------------------
void ofxThreadedImageLoaderSingleton::loadFromURL(ofImage& image, string url) {
  instance();
	__instance->nextID++;
	ofImageLoaderEntry entry(image, OF_LOAD_FROM_URL);
	entry.url = url;
	entry.id = __instance->nextID;
	entry.image->setUseTexture(false);	
	entry.name = "image" + ofToString(entry.id);

    __instance->lock();
	__instance->images_to_load_buffer.push_back(entry);
    __instance->condition.signal();
    __instance->unlock();
}


// Reads from the queue and loads new images.
//--------------------------------------------------------------
void ofxThreadedImageLoaderSingleton::threadedFunction() {
    deque<ofImageLoaderEntry> images_to_load;

	while( isThreadRunning() ) {
		lock();
		if(images_to_load_buffer.empty()) condition.wait(mutex);
		images_to_load.insert( images_to_load.end(),
							images_to_load_buffer.begin(),
							images_to_load_buffer.end() );

		images_to_load_buffer.clear();
		unlock();
        
        
        while( !images_to_load.empty() ) {
            ofImageLoaderEntry  & entry = images_to_load.front();
            
            if(entry.type == OF_LOAD_FROM_DISK) {
                if(! entry.image->loadImage(entry.filename) )  { 
                    ofLogError() << "ofxThreadedImageLoaderSingleton error loading image " << entry.filename;
                }
                
                lock();
                images_to_update.push_back(entry);
                unlock();
            }else if(entry.type == OF_LOAD_FROM_URL) {
                lock();
                images_async_loading.push_back(entry);
                unlock();
                
                ofLoadURLAsync(entry.url, entry.name);
            }

    		images_to_load.pop_front();
        }
	}
}


// When we receive an url response this method is called; 
// The loaded image is removed from the async_queue and added to the
// update queue. The update queue is used to update the texture.
//--------------------------------------------------------------
void ofxThreadedImageLoaderSingleton::urlResponse(ofHttpResponse & response) {
	if(response.status == 200) {
		lock();
		
		// Get the loaded url from the async queue and move it into the update queue.
		entry_iterator it = getEntryFromAsyncQueue(response.request.name);
		if(it != images_async_loading.end()) {
			(*it).image->loadImage(response.data);
			images_to_update.push_back(*it);
			images_async_loading.erase(it);
		}
		
		unlock();
	}else{
		// log error.
		ofLogError()<< "Could not get image from url, response status: " << response.status;
		ofRemoveURLRequest(response.request.getID());
		// remove the entry from the queue
		lock();
		entry_iterator it = getEntryFromAsyncQueue(response.request.name);
		if(it != images_async_loading.end()) {
			images_async_loading.erase(it);
		}
		unlock();
	}
}


// Check the update queue and update the texture
//--------------------------------------------------------------
void ofxThreadedImageLoaderSingleton::update(ofEventArgs & a){
    
    // Load 1 image per update so we don't block the gl thread for too long
    
    lock();
	if (!images_to_update.empty()) {

		ofImageLoaderEntry entry = images_to_update.front();

		const ofPixels& pix = entry.image->getPixelsRef();
		entry.image->getTextureReference().allocate(
				 pix.getWidth()
				,pix.getHeight()
				,ofGetGlInternalFormat(pix)
		);
		
		entry.image->setUseTexture(true);
		entry.image->update();
    ofNotifyEvent(imageLoadedEvent, *(entry.image), this); 

		images_to_update.pop_front();
	}
    unlock();

}


// Find an entry in the aysnc queue.
//   * private, no lock protection, is private function
//--------------------------------------------------------------
ofxThreadedImageLoaderSingleton::entry_iterator ofxThreadedImageLoaderSingleton::getEntryFromAsyncQueue(string name) {
	entry_iterator it = images_async_loading.begin();
	for(;it != images_async_loading.end();it++) {
		if((*it).name == name) {
			return it;
		}
	}
	return images_async_loading.end();
}
