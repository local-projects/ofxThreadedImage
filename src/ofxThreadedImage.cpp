
#include "ofxThreadedImage.h"
#if (USE_OPENCV_TO_RESIZE)
#include "ofxOpenCv.h"
#endif


int ofxThreadedImage::numAlive = 0;
int ofxThreadedImage::numLoading = 0;

ofxThreadedImage::ofxThreadedImage(){
	pendingTexture = false;
	timeOut = 10;
	alpha = 1;
	alphaRiseSpeed = 0.05; //20 frames to full alpha
	imageLoaded = false;
	pendingNotification = false;
	readyToDraw = false;
	problemLoading = false;
	resizeAfterLoad = false;
	compression = OF_COMPRESS_NONE;
	busy = false;
	numAlive++;
}

ofxThreadedImage::~ofxThreadedImage(){
	if (isThreadRunning()){
		try {
			waitForThread(true);
		}
		catch (std::exception& e) {
			ofLogError("ofxThreadedImage", "Exception at waitForThread %s\n", e.what() );
		}
	}
	numAlive--;
}

void ofxThreadedImage::resizeIfNeeded(){
	if (resizeAfterLoad){
		int w = getWidth();
		int h = getHeight();
		int largestSide = MAX(w, h);
		if(largestSide > maxSideSize){ //we need resize!
			float scale = maxSideSize / (float)largestSide;
			//float t1 = ofGetElapsedTimef();
			int newW = w * scale;
			int newH = h * scale;
			#if USE_OPENCV_TO_RESIZE
			if (type == OF_IMAGE_COLOR){ //TODO cover rgba!
				ofxCvColorImage cvImg;
				cvImg.setUseTexture(false);
				cvImg.allocate(w, h);
				cvImg.setFromPixels(getPixels(), w, h);
				ofxCvColorImage cvImgsmall;
				cvImgsmall.setUseTexture(false);
				cvImgsmall.allocate(newW, newH);
				cvImgsmall.scaleIntoMe(cvImg, CV_INTER_AREA);
				setFromPixels(cvImgsmall.getPixels(), newW, newH, OF_IMAGE_COLOR);
			}else{
				resize(newW, newH);
			}
			#else
			resize(newW, newH); //opencv resizing is much faster!
			#endif
			//ofLog() << "time resize: " + ofToString( ofGetElapsedTimef() - t1 );
		}
	}
}

void ofxThreadedImage::constrainImageSize(int largestSide){
	maxSideSize = largestSide;
	resizeAfterLoad = true;
}

void ofxThreadedImage::threadedFunction(){

	numLoading++;
	ofThread::getPocoThread().setName("ofxThreadedImage");

	if( lock() ){

		switch (whatToDo) {
			case SAVE:
				ofSaveImage(getPixelsRef(), fileName, quality);
				break;

			case LOAD:{
				alpha = 0.0f;
				//float t1 = ofGetElapsedTimef();
				loadImageBlocking(fileName);
				//ofLog() << "time to load: " << ofGetElapsedTimef() - t1;
				}break;

			case LOAD_HTTP:
				alpha = 0;
				ofxSimpleHttp http;
				http.setTimeOut(timeOut);
				ofxSimpleHttpResponse response = http.fetchURLtoDiskBlocking(url, IMG_DOWNLOAD_FOLDER_NAME);
				if (response.ok){

					setUseTexture(false);
					bool loaded = loadImage(response.absolutePath);
					if (loaded){
						resizeIfNeeded();
						imageLoaded = true;
					}else{
						ofLog(OF_LOG_ERROR, "loadHttpImageBlocking() failed to load from disk (%d) > %s\n", response.status, url.c_str() );
					}
				}else{
					ofLog(OF_LOG_ERROR, "loadHttpImageBlocking() failed to download (%d) > %s\n", response.status, url.c_str() );
				}
				break;
		}
		unlock();

	} else {
		ofLogError("ofxThreadedImage::threadedFunction Can't %s %s, thread is already running", whatToDo == SAVE ? "Save" : "Load",  fileName.c_str() );
	}
	stopThread();
	numLoading--;

	//fixed in OF 0.8.3
//	#if  defined(TARGET_OSX) || defined(TARGET_LINUX) /*I'm not 100% sure of linux*/
//	pthread_detach( pthread_self() ); //this is a workaround for this issue https://github.com/openframeworks/openFrameworks/issues/2506
//	#endif

}


bool ofxThreadedImage::loadImageThreaded(string fileName_){

	if(!busy){
		busy = true;
		ofAddListener(ofEvents().update, this, &ofxThreadedImage::_update);
		alpha = 0.0;
		whatToDo = LOAD;
		fileName = fileName_;
		readyToDraw = false;
		problemLoading = false;
		startThread(true);
		return true;
	}else{
		return false;
	}
}


bool ofxThreadedImage::loadImageBlocking(string fileName){

	ofAddListener(ofEvents().update, this, &ofxThreadedImage::_update);
	imageLoaded = false;
	whatToDo = LOAD;
	problemLoading = false;
	setUseTexture(false);
	alpha = 0.0;
	bool loaded = loadImage(fileName);
	if (!loaded){
		ofLogError() << "ofxThreadedImage:: img couldnt load! \"" << fileName << "\"" << endl;
		problemLoading = true;
		pendingNotification = true;
	}else{
		resizeIfNeeded();
		imageLoaded = true;
	}
	pendingTexture = true;
	return loaded;
}


bool ofxThreadedImage::loadHttpImageBlocking(string url_){
	if(!busy){
		ofAddListener(ofEvents().update, this, &ofxThreadedImage::_update);
		alpha = 0;
		whatToDo = LOAD_HTTP;
		url = url_;
		readyToDraw = false;
		problemLoading = false;
		setUseTexture(false);
		ofxSimpleHttp http;
		http.setTimeOut(timeOut);
		ofxSimpleHttpResponse response = http.fetchURLBlocking(url);
		if (response.ok){
			ofDirectory dir;
			dir.open(ofToDataPath(IMG_DOWNLOAD_FOLDER_NAME, false));
			if ( !dir.exists()){
				dir.create();
			}
			string filePath = ofToDataPath( (string)IMG_DOWNLOAD_FOLDER_NAME + "/" + response.fileName, false );
			FILE * file = fopen( filePath.c_str(), "wb");
			fwrite (response.responseBody.c_str() , 1 , response.responseBody.length() , file );
			fclose( file);
		}else{
			ofLog(OF_LOG_ERROR, "loadHttpImageBlocking() failed (%d) > %s\n", response.status, url.c_str() );
			return false;
		}
		imageLoaded = false;
		bool ok = loadImage((string)IMG_DOWNLOAD_FOLDER_NAME + "/" + response.fileName);
		if(ok){
			resizeIfNeeded();
			imageLoaded = true;
		}
		pendingTexture = true;
		return ok;
	}return false;
}


bool ofxThreadedImage::loadHttpImageThreaded(string url_){
	if(!busy){
		ofAddListener(ofEvents().update, this, &ofxThreadedImage::_update);
		alpha = 0;
		whatToDo = LOAD_HTTP;
		url = url_;
		pendingTexture = true;
		imageLoaded = false;
		problemLoading = false;
		readyToDraw = false;
		setUseTexture(false);
		startThread(true);
		return true;
	}else{
		return false;
	}
}


bool ofxThreadedImage::isReadyToDraw(){
	return readyToDraw;
}


bool ofxThreadedImage::arePixelsAvailable(){
	return imageLoaded;
}


void ofxThreadedImage::updateTextureIfNeeded(){
	if (pendingTexture){
		if (!problemLoading){
			setUseTexture(true);
			tex.setCompression(compression);
			tex.allocate(getPixelsRef());
			//tex.setTextureMinMagFilter(GL_LINEAR, GL_LINEAR_MIPMAP_LINEAR);
			ofImage::update();
			readyToDraw = true;
			pendingTexture = false;
		}
		pendingNotification = true; //texture is loaded, notify owner!
	}
}


bool ofxThreadedImage::saveThreaded(string where, ofImageQualityType quality_){
	if(!busy){
		whatToDo = SAVE;
		this->fileName = where;
		this->quality = quality_;
		startThread(false);   // !blocking
		return true;
	} return false;
};


void ofxThreadedImage::setTexCompression(ofTexCompression c){
	compression = c;
}

void ofxThreadedImage::_update(ofEventArgs &e){

	if(imageLoaded){
		updateTextureIfNeeded();
	}

	if(pendingNotification){
		ofxThreadedImageEvent event;
		event.image = this;
		if(problemLoading){
			event.loaded = false;
			imageLoaded = false;
		}
		ofNotifyEvent( imageReadyEvent, event, this );
		pendingNotification = false;
		ofRemoveListener(ofEvents().update, this, &ofxThreadedImage::_update);
		busy = false;
	}
}


void ofxThreadedImage::draw(float _x, float _y, bool fadeInOnDelayedLoad){
	ofxThreadedImage::draw(_x, _y, getPixelsRef().getWidth(), getPixelsRef().getHeight(), fadeInOnDelayedLoad );
}


void ofxThreadedImage::draw(float _x, float _y, float _w, float _h, bool fadeInOnLoad){

	if (imageLoaded && fadeInOnLoad && alpha < 1.0f){
		if (whatToDo == LOAD_HTTP || whatToDo == LOAD){
			alpha += alphaRiseSpeed;
			if(alpha > 1.0) alpha = 1.0;
		}

		ofPushStyle();
			ofSetColor(255,255,255, 255 * alpha);
			ofImage::draw(_x, _y, _w, _h);
        ofPopStyle();

	}else{
		if(tex.bAllocated()){
			ofImage::draw(_x, _y, _w, _h);
		}
	}
}

