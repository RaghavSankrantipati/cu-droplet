#include "droplet_programs/positions.h"

void init(){
	if((RNB_DUR+PADDLE_MSG_DUR+NEIGHB_MSG_DUR+BALL_MSG_DUR)>=(SLOT_LENGTH_MS)){
		printf_P(PSTR("You've got problems! SLOT_LENGTH_MS needs to be longer than all the things that take place during a slot!\r\n"));
	}
	numNearBots = 0;
	loopID = 0xFFFF;
	frameCount = 0;
	frameStart=get_time();
	for(uint8_t i=0;i<NUM_TRACKED_BOTS;i++){
		cleanOtherBot(&nearBots[i]);
	}
	myState = STATE_PIXEL;
	colorMode = POS;
	gameMode = BOUNCE;
	lastBallID = 0;
	lastBallMsg = 0;
	//lastPaddleMsg = 0;
	lastLightCheck = get_time();
	initPositions();
	mySlot = (get_droplet_id()%(SLOTS_PER_FRAME-1));
	//mySlot = get_droplet_ord(get_droplet_id());
	printf("mySlot: %u, frame_length: %lu\r\n", mySlot, FRAME_LENGTH_MS);
	frameEndPrintout();
	hardBotsList = NULL;
	isCovered = 0;
}

void loop(){
	uint32_t frameTime = get_time()-frameStart;
	if(frameTime>FRAME_LENGTH_MS){
		frameTime = frameTime - FRAME_LENGTH_MS;
		frameStart += FRAME_LENGTH_MS;
		frameCount++;
	}
	if(loopID!=(frameTime/SLOT_LENGTH_MS)){
		loopID = frameTime/SLOT_LENGTH_MS;
		if(loopID==mySlot){
			uint32_t before = get_time();
			handleMySlot();
			GEN_DEBUG_PRINT("My slot processing took %lu ms.\r\n", get_time()-before);
		}else if(loopID==SLOTS_PER_FRAME-1){
			uint32_t before = get_time();
			handleFrameEnd();
			GEN_DEBUG_PRINT("End of Frame Processing/Printing Took %lu ms.\r\n",get_time()-before);
		}
		if(loopID!=mySlot){
			uint32_t curSlotTime = (get_time()-frameStart)%SLOT_LENGTH_MS;
			//if(NS_PIXEL(myState) && paddleChange>=1.0){
				////schedule_task((RNB_DUR-curSlotTime),sendPaddleMsg(), NULL);
			//}else{
				//paddleChange = 0.0;
			////}
			//if(/*myState<=2 &&*/ myDist!=UNDF && otherDist!=UNDF && myDist<otherDist){
				//uint32_t curSlotTime = (get_time()-frameStart)%SLOT_LENGTH_MS;
				//schedule_task(((RNB_DUR+PADDLE_MSG_DUR+NEIGHB_MSG_DUR)-curSlotTime), sendBallMsg, NULL);
			//}
		}
		updateBall();
		updateColor();
	}
	//These things happen every single loop: once every LOOP_DELAY_MS.
	if(NS_PIXEL(myState)){
		checkLightLevel();
	}
	if(rnb_updated){
		useNewRnbMeas();
	}
	delay_ms(LOOP_DELAY_MS);
}

void handleMySlot(){
	broadcast_rnb_data();
	while(((get_time()-frameStart)%SLOT_LENGTH_MS)<RNB_DUR)
		delay_us(500);
	//if(NS_PIXEL(myState) && paddleChange>=1.0){
////		sendPaddleMsg();
	//}else{
		//paddleChange = 0.0;
	//}
	while(((get_time()-frameStart)%SLOT_LENGTH_MS)<(RNB_DUR+PADDLE_MSG_DUR))
		delay_us(500);		
	sendNearBotsMsg();
	while(((get_time()-frameStart)%SLOT_LENGTH_MS)<(RNB_DUR+PADDLE_MSG_DUR+NEIGHB_MSG_DUR))
		delay_us(500);			
	//if(myDist!=UNDF && otherDist!=UNDF && myDist<otherDist){
		//sendBallMsg();
	//}
	while(((get_time()-frameStart)%SLOT_LENGTH_MS)<(RNB_DUR+PADDLE_MSG_DUR+NEIGHB_MSG_DUR+BALL_MSG_DUR))
		delay_us(500);	
}

void handleFrameEnd(){
	qsort(nearBots, NUM_TRACKED_BOTS, sizeof(OtherBot), nearBotsConfCmpFunc);
	//Maybe we'll want to remove the N worst nearBots, here.
	if(!seedFlag){
		updatePos();
	}
	
	updateHardBots();
	degradeConfidence(); //lower confidence of bots for which no measurement was received.
	frameEndPrintout();	
	printf("\r\n");
}

void updateHardBots(){
	//First, making a copy of nearBots so we can sort it by a weird metric.
	BotMeas nearBotsMeas[numNearBots];
	for(uint8_t i=0;i<numNearBots;i++){
		nearBotsMeas[i].id   = nearBots[i].meas.id;
		nearBotsMeas[i].r    = nearBots[i].meas.r;
		nearBotsMeas[i].b    = nearBots[i].meas.b;
		nearBotsMeas[i].h    = nearBots[i].meas.h;
		nearBotsMeas[i].conf = nearBots[i].meas.conf;
	}
	cleanHardBots(); //clean out the previous hardBots list -- we start fresh each farme.
	//sort nearBots according to their bearing.
	qsort(nearBotsMeas, numNearBots, sizeof(BotMeas), nearBotMeasBearingCmpFunc);
	//go through each near bot by bearing and add it to hardBots if the gap in bearings is above 120 degrees
	for(uint8_t i=0;i<numNearBots;i++){
		uint8_t nextI = (i+1)%numNearBots;
		uint8_t difference = abs((nearBotsMeas[i].b-nearBotsMeas[nextI].b + 540)%360 - 180) ;
		if(difference>120){
			addHardBot(nearBotsMeas[i].id);
			addHardBot(nearBotsMeas[nextI].id);
		}
	}
	//print out hard bots list.
	if(hardBotsList!=NULL){
		BALL_DEBUG_PRINT("Edges:");
		HardBot* tmp = hardBotsList;
		while(tmp!=NULL){
			BALL_DEBUG_PRINT("\t%04X", tmp->id);
			tmp = tmp->next;
		}
		BALL_DEBUG_PRINT("\r\n");
	}
}

void degradeConfidence(){
	//confidence degrades if we don't get new measurements.
	for(uint8_t i=0;i<NUM_TRACKED_BOTS;i++){
		if(nearBots[i].meas.conf>0){
			nearBots[i].meas.conf>>=1;
			for(uint8_t j=0;j<NUM_SHARED_BOTS;j++){
				if(nearBots[i].shared[j].conf >0){
					nearBots[i].shared[j].conf = nearBots[i].shared[j].conf>>1;
				}else{
					nearBots[i].shared[j].id = 0;
				}
			}
		}else{
			removeOtherBot(i);
		}
	}
}

void updatePos(){
	//nearBots is array of OtherBot structs, each containing:
	//    BotMeas meas: Our measurement of this robot, containing:
	//	      id, r, b, h, conf
	//    BotPos pos: position of this robot, in its estimation.
	//        x, y  //either of which may be undefined (UNDF)
	//    BotMeas[] shared: this robot's measurements of other robots (four best).
	//        Each meas:
	//            id, r, b, h, conf
	
	/*
	Just going to be doing Batch Linear Least Squares with all measurements at the end of each frame, 
	on all the (x, y) positions I would have based on each measurement.
	This isn' t the measurements directly, which hides error relationships, but will hopefully work out.
	(simplifies to waited average; don't have to deal with matrices directly.)
	*/
	POS_DEBUG_PRINT("In updatePos()\r\n");
	BotMeas* meas;
	BotMeas* theirMeas;
	BotPos* theirPos;
	float totalConf = 0;
	uint8_t anyInfo = 0;
	uint8_t themToMeFound = 0;
	float xEst = 0;
	float yEst = 0;
	float thisX, thisY;
	float measConf, measVar;
	float theirMeasConf, theirMeasVar;
	float theirPosConf, theirPosVar;
	float bearingFromUs;
	float combinedHeading;
	float combinedRange;
	float combinedBearing;
	float combinedHeadingConf;
	float bearingFromUsConf;

	for(uint8_t i=0;i<NUM_TRACKED_BOTS;i++){
		meas = &(nearBots[i].meas);
		theirPos = &(nearBots[i].pos);
		if( theirPos->x != UNDF && theirPos->y != UNDF){
			//First handle my measurement of this bot.
			theirPosConf = (float)(theirPos->conf);
			theirPosConf *= theirPosConf;
			theirPosVar = (1.0/theirPosConf);
			measConf = (float)(meas->conf);
			measConf *= measConf;
			measVar  = (1.0/measConf);
			themToMeFound = 0;
			for(uint8_t j=0;j<NUM_SHARED_BOTS;j++){
				theirMeas = &(nearBots[i].shared[j]);
				if(theirMeas->id == get_droplet_id() && theirMeas->conf > 0){ //Found a measurement of me!
					themToMeFound = 1;
					theirMeasConf = theirMeas->conf;
					theirMeasConf *= theirMeasConf;
					theirMeasVar = (1.0/theirMeasConf);
					break; //(There will be only one measurement of me)		
				}
			}
			anyInfo++;
			if(themToMeFound){ //If we have this, we'll combine our two measurements first.
				combinedRange = (measConf*meas->r + theirMeasConf*theirMeas->r)/(measConf+theirMeasConf);
				//combinedHeading is in degrees, and is from this droplet's perspective.
				combinedHeading = (measConf*meas->h - theirMeasConf*theirMeas->h)/(measConf+theirMeasConf);
				combinedHeadingConf = 2.0/(measVar+theirMeasVar);
				//bearingFrom us is in radians, and uses combinedHeading to convert meas->b (bearing from our perspective) 
				//to bearing in other bot's perspective.
				bearingFromUs = deg_to_rad(meas->b-combinedHeading+180);
				bearingFromUsConf = 1.0/(measVar+(1.0/combinedHeadingConf));
				//combinedBearing is in radians, and is from the other Droplet's perspective.
				combinedBearing = (bearingFromUsConf*bearingFromUs + theirMeasConf*deg_to_rad(theirMeas->b))/(bearingFromUsConf+theirMeasConf);
				thisX = combinedRange*cos(combinedBearing+M_PI_2);
				thisY = combinedRange*sin(combinedBearing+M_PI_2);
				measVar = (1.0/combinedHeadingConf)+theirPosVar;
				measConf = 1.0/measVar; 
				POS_DEBUG_PRINT("    \tcRange: %4d, cB: %4d, cH: %4d\r\n", (int16_t)combinedRange, (int16_t)rad_to_deg(combinedBearing), (int16_t)combinedHeading);
				POS_DEBUG_PRINT("    \tdeltaX: %4d, deltaY: %4d, X: %4d, Y: %4d, thC: %3hd\r\n", (int16_t)thisX, (int16_t)thisY, (int16_t)(theirPos->x + thisX), (int16_t)(theirPos->y + thisY), (int8_t)sqrt(measConf));
				xEst += ((theirPos->x + thisX)*measConf);
				yEst += ((theirPos->y + thisY)*measConf);
				totalConf += measConf;
			}else{
				thisX = ((float)(meas->r)) * cos(deg_to_rad(meas->b) - M_PI_2 - deg_to_rad(meas->h));
				thisY = ((float)(meas->r)) * sin(deg_to_rad(meas->b) - M_PI_2 - deg_to_rad(meas->h));
				measVar += theirPosVar;
				measConf = 1.0/measVar;
				xEst += ((theirPos->x + thisX)*measConf);
				yEst += ((theirPos->y + thisY)*measConf);
				totalConf += measConf;
				POS_DEBUG_PRINT("%04X\ttheirX: %4d, theirY: %4d\r\n\tdeltaX: %4d, deltaY: %4d, X: %4d, Y: %4d, myC: %3hd | h: %4d\r\n", meas->id, theirPos->x, theirPos->y, (int16_t)thisX, (int16_t)thisY, (int16_t)(theirPos->x + thisX), (int16_t)(theirPos->y + thisY), (int8_t)sqrt(measConf), meas->h);
			}
		}
	}

	/*
	 * Now we've thrown all of our data in to xEst, yEst, and totalConf.
	 * anyInfo is the number of measurements combined. 
 	 */
	if(anyInfo){ //got new pos information this frame!
		POS_DEBUG_PRINT("Got info this frame!\r\n");
		float overallConf = totalConf/anyInfo;
		float newX = (xEst/totalConf);
		float newY = (yEst/totalConf);
	
		if(myPos.x!=UNDF && myPos.y!=UNDF){ //pos was defined, so we're updating with this frame's info.
			float myPosConf = (float)(myPos.conf);
			myPosConf *= myPosConf;			
			POS_DEBUG_PRINT("\tNew Pos: (%d, %d), newPosConf: %hd | prevPos: (%d, %d), prevPosConf: %hd\r\n", (int16_t)newX, (int16_t)newY, (int8_t)(sqrt(overallConf)+0.5), myPos.x, myPos.y, myPos.conf);								
			myPos.x = (int16_t)((newX*overallConf + myPos.x*myPosConf)/(overallConf + myPosConf));
			myPos.y = (int16_t)((newY*overallConf + myPos.y*myPosConf)/(overallConf + myPosConf));
			myPos.conf = (int8_t)(sqrt(2/((1.0/overallConf)+(1.0/myPosConf)))+0.5); //extra 0.5 to help with rounding.
			POS_DEBUG_PRINT("\t\tResult: (%d, %d), conf: %hd\r\n", myPos.x, myPos.y, myPos.conf);
		}else{ //Position previously undefined, so this is our first pos.
			POS_DEBUG_PRINT("\tPos previously undefined!\r\n");
			myPos.x = (int16_t)(newX);
			myPos.y = (int16_t)(newY);
			myPos.conf = (int8_t)(sqrt(overallConf)+0.5);
			POS_DEBUG_PRINT("\t\tResult: (%d, %d), conf: %hd\r\n", myPos.x, myPos.y, myPos.conf);		
		}
				
	}else{ //no new pos information this frame.
		POS_DEBUG_PRINT("No new info this frame.\r\n");
		//if position was defined, our confidence in that position will degrade.
		if(myPos.x!=UNDF && myPos.y!=UNDF){
			POS_DEBUG_PRINT("\tDegrading confidence...\r\n");			
			myPos.conf>>=1;
			if(!myPos.conf){
				myPos.x = UNDF;
				myPos.y = UNDF;
			}
			POS_DEBUG_PRINT("\t\tResult: (%d, %d), conf: %hd\r\n", myPos.x, myPos.y, myPos.conf);			
		}
	}
}

void useNewRnbMeas(){

	//Pulling everything out of the global struct.
	uint16_t id = last_good_rnb.id_number;
	uint16_t range = last_good_rnb.range*10; //converting to mm
	int16_t bearing = rad_to_deg(last_good_rnb.bearing);
	int16_t heading = rad_to_deg(last_good_rnb.heading);
	int8_t conf = (int8_t)(sqrt(last_good_rnb.conf+1.0)+0.5);
	rnb_updated=0;
	if(conf<=2) return;
	conf <<= 1; //
	conf = conf>63 ? 63 : conf;
	RNB_DEBUG_PRINT("(RNB) ID: %04X\r\n\tR: %4u B: %4d H: %4d | %4hd\r\n", id, range, bearing, heading, conf);
	OtherBot* measuredBot = addOtherBot(last_good_rnb.id_number, conf);
	BotMeas* meas;
	if(measuredBot){
		meas = &(measuredBot->meas);
	}else{
		return;
	}
	if(meas->id == 0){
		//We weren't tracking this ID before, so just add the new info.
		meas->id	= id;
		meas->r		= range;
		meas->b		= bearing;
		meas->h		= heading;
		meas->conf  = conf;
	}else if(meas->id == last_good_rnb.id_number){
		//Also just adding the new info in this case.
		//Maybe want to do something smarter, eventually???
		//	  Averaging, but that's not great if bots are moving.
		//    Some kind of smart average where we look at confidence
		//    and how big the difference between the two is to
		//    to determine how best to handle it.
		float newConfSqrd = powf(conf,2);
		float prevConfSqrd = powf(meas->conf, 2);
		float totalConf = newConfSqrd + prevConfSqrd;
		float newConf = sqrt(totalConf/2) + 0.5;
		float newRange = ((meas->r)*prevConfSqrd + range*newConfSqrd)/totalConf;
		float newB = ((meas->b)*prevConfSqrd + bearing*newConfSqrd)/totalConf;
		float newH = ((meas->h)*prevConfSqrd + heading*newConfSqrd)/totalConf;
		meas->id	= id;
		meas->r		= (uint16_t)newRange;
		meas->b		= (int16_t)newB;
		meas->h		= (int16_t)newH;
		meas->conf  = (int8_t)newConf;
		RNB_DEBUG_PRINT("\tR: %4u B: %4d H: %4d | %4hd\r\n", meas->r, meas->b, meas->h, meas->conf);
	}else{
		printf_P(PSTR("Error: Unexpected botPos->ID in use_new_rnb_meas.\r\n"));
	}
}

void updateBall(){
	if(theBall.lastUpdate){
		uint32_t now = get_time();
		int32_t timePassed = now-theBall.lastUpdate;
		if(myPos.x!=UNDF && myPos.y!=UNDF && theBall.xPos!=UNDF && theBall.yPos!=UNDF){

			//int8_t crossedBefore = checkBallCrossedMe();

			//int8_t crossedAfter = checkBallCrossedMe();
			//myDist = (uint16_t)hypotf(theBall.xPos-myPos.x,theBall.yPos-myPos.y);			
			//theBall.lastUpdate = now;			
			//if(myDist<=30 && crossedBefore!=crossedAfter){ //BOUNCE CHECK
				//uint8_t ballInPaddle = ((theBall.xPos+theBall.radius)>=paddleStart && (theBall.xPos-theBall.radius)<=paddleEnd);
				//uint8_t ballLeaving = (NORTH_PIXEL(myState) && theBall.yVel<0) || (SOUTH_PIXEL(myState) && theBall.yVel>0);
				//if(gameMode==PONG && !ballInPaddle && ballLeaving){
					//killBall();
				//}else{
					//check_bounce(theBall.xVel, theBall.yVel, &(theBall.xVel), &(theBall.yVel));
				//}
			//}else{
			theBall.xPos += (int16_t)((((int32_t)(theBall.xVel))*timePassed)/1000.0);
			theBall.yPos += (int16_t)((((int32_t)(theBall.yVel))*timePassed)/1000.0);
			theBall.lastUpdate = now;			
			BALL_DEBUG_PRINT("B[%hu]: %d, %d\r\n", theBall.id, theBall.xPos, theBall.yPos);
			uint8_t bounced = 0;
			HardBot* tmp = hardBotsList;
			myDist = (uint16_t)hypotf(myPos.x-theBall.xPos, myPos.y-theBall.yPos);
			while(tmp!=NULL){
				OtherBot* bot = getOtherBot(tmp->id);			
				if(myDist<(((bot->meas).r*10)/6)){
					BALL_DEBUG_PRINT("\t%04X | ", tmp->id);
					if(checkBounceHard((bot->pos).x,(bot->pos).y, timePassed)){
						if(gameMode==PONG && ((SOUTH_PIXEL(myState) && theBall.yVel<=0) || (NORTH_PIXEL(myState) && theBall.yVel>=0))){
							if(!isCovered){
								//Other Side scores a point!
								killBall();
								set_rgb(255,0,0);
							}
						}
						calculateBounce((bot->pos).x,(bot->pos).y);
						BALL_DEBUG_PRINT("Ball bounced off boundary between me and %04X!\r\n", tmp->id);
						otherDist = (((bot->meas).r*10)/6);
						bounced = 1;
						break;
					}
				}
				tmp = tmp->next;
			}
			if(theBall.xPos<MIN_X || theBall.xPos>MAX_X || theBall.yPos<MIN_Y || theBall.yPos>MAX_Y){
				BALL_DEBUG_PRINT("Ball hit boundary, so we must have lost track.\r\n");
				theBall.xPos = UNDF;
				theBall.yPos = UNDF;
				myDist = UNDF;
				otherDist = UNDF;
			}
		}else{
			myDist = UNDF;
			otherDist = UNDF;
		}
	}
}

//void check_bounce(int8_t xVel, int8_t yVel, int8_t* newXvel, int8_t* newYvel){
	//float inAngle = atan2(yVel, xVel)-M_PI_2;
////	float inVel = hypotf(xVel, yVel);
	//uint8_t in_dir = dirFromAngle(inAngle+180);
	//BALL_DEBUG_PRINT(PSTR("In check bounce:\r\n"));
	//BALL_DEBUG_PRINT(PSTR("\tIn angle: %f, inDir: %hu, xVel: %hd, yVel: %hd\r\n"), rad_to_deg(inAngle), in_dir, xVel, yVel);
	//BALL_DEBUG_PRINT("Note! check_bounce currently doesn't do anything! Eventually, it will look to see if there's a robot in direction 'inAngle'.\r\n");
//}

void updateColor(){
	uint8_t newR = 0, newG = 0, newB = 0;
	if(colorMode==POS){
		if(myPos.x==UNDF||myPos.y==UNDF){
			newR = newG = newB = 50;
		}else{
			int16_t xColVal = (int16_t)(6.0*pow(41.0,(myPos.x-MIN_X)/((MAX_X-MIN_X)*1.0))+9.0);
			int16_t yColVal = (int16_t)(3.0*pow(84.0,(myPos.y-MIN_Y)/((MAX_Y-MIN_Y)*1.0))+3.0);
			newR = (uint8_t)(xColVal);
			newG = (uint8_t)(yColVal);
		}
	}else if(colorMode==SYNC_TEST){
		//printf("Frame: %2lu ; loopID: %2u (mod: %2u)", frameCount, loopID, loopID%30);
		if((loopID%36)<24){
			//printf(" || %hu", (uint8_t)((loopID%36)>>2));
			hsv_to_rgb((uint16_t)(60*((loopID%36)>>2)), 220, 127, &newR, &newG, &newB);
		}else{
			float value = (frameCount%6)/6.0;
			uint8_t val = (uint8_t)(5.0*pow(51.0,value));
			hsv_to_rgb(0,0, val,&newR, &newG, &newB);
		}
		//printf("\r\n");
	}
	if(myPos.x!=UNDF && myPos.y!=UNDF){
		float coverage = getBallCoverage() /*+ getPaddleCoverage()*/;
		coverage = (coverage > 1.0) ? 1.0 : coverage;
		uint8_t intensityIncrease = 0;
		if(coverage>0.01){
			intensityIncrease = (uint16_t)(5.0*pow(51.0,coverage));
		}
		uint16_t newRed		= newR + intensityIncrease;
		uint16_t newGreen	= newG + intensityIncrease;
		uint16_t newBlue	= newB + intensityIncrease;
		newR = newRed>255 ? 255 :  newRed;
		newG = newGreen>255 ? 255 : newGreen;
		newB = newBlue>255 ? 255 : newBlue;
	}
	set_rgb(newR, newG, newB);	
}

float getBallCoverage(){
	float ballCoveredRatio = 0.0;
	if(myDist!=UNDF && myDist<(DROPLET_RADIUS+theBall.radius) && theBall.id!=0x0F){
		if(theBall.radius<DROPLET_RADIUS){
			if(myDist>=(DROPLET_RADIUS-theBall.radius)){
				ballCoveredRatio = getCoverageRatioA(theBall.radius, myDist);
			}else{
				ballCoveredRatio = 1.0;
			}
		}else{
			if(myDist>=(theBall.radius-DROPLET_RADIUS)){
				ballCoveredRatio = getCoverageRatioB(theBall.radius, myDist);
			}else{
				ballCoveredRatio = 1.0;
			}
		}
	}
	return ballCoveredRatio;
	//printf("Ball Coverage:\t%f | me: (%5.1f, %5.1f) ball: (%5.1f, %5.1f)->%hu\r\n", ballCoveredRatio, myX, myY, theBall.xPos, theBall.yPos, theBall.radius);	
}

//float getPaddleCoverage(){
	//float paddleCoveredRatio = 0.0;
	//if(gameMode==PONG && NS_PIXEL(myState) ){
		//int16_t myStart = myPos.x-10*DROPLET_RADIUS; //converting cm to mm. 
		//int16_t myEnd   = myPos.x+10*DROPLET_RADIUS; //converting cm to mm.
		//if(myEnd>paddleStart && paddleEnd>myStart){ //otherwise, no intersection
			//if(myEnd>paddleEnd){
				//if(myStart>paddleStart){
					//paddleCoveredRatio = (1.0*(paddleEnd-myStart))/(1.0*(myEnd-myStart));
				//}else{
					//paddleCoveredRatio = (1.0*(paddleEnd-paddleStart))/(1.0*(myEnd-myStart));
				//}
			//}else{
				//if(myStart>paddleStart){
					//paddleCoveredRatio = 1.0; //my end-myStart
				//}else{
					//paddleCoveredRatio = (1.0*(myEnd-paddleStart))/(1.0*(myEnd-myStart));
				//}
			//}
		//}
	//}
	//return paddleCoveredRatio;
	////printf("Paddle Coverage:\t%f | me: (%5.1f, %5.1f) ball: (%5.1f, %5.1f)->%hu\r\n", paddleCoveredRatio, myX, myY, theBall.xPos, theBall.yPos, theBall.radius);	
//}

void checkLightLevel(){
	int16_t r, g, b;
	get_rgb(&r,&g,&b);
	int16_t sum = r+g+b;
	uint32_t now = get_time();
	//if(sum<=25){
		//paddleChange += ((now-lastLightCheck)*(3*PADDLE_VEL));
		////printf("Paddle ChangeX5: %f\r\n", paddleChange);
	//}else if(sum<=40){
		//paddleChange += ((now-lastLightCheck)*(2*PADDLE_VEL));
		////printf("Paddle ChangeX3: %f\r\n", paddleChange);
	//}else if(sum<=60){
		//paddleChange += ((now-lastLightCheck)*(PADDLE_VEL));
		////printf("Paddle ChangeX5: %f\r\n", paddleChange);
	//}
	if(sum<=60){
		isCovered=1;
	}else{
		isCovered=0;
	}
	lastLightCheck = now;
	//printf("Light: %5d (%4d, %4d, %4d)\r\n",sum,r,g,b);
}

void sendBallMsg(){
	if(myDist==UNDF || myDist>=30){
		return;
	}
	BallMsg msg;
	msg.flag = BALL_MSG_FLAG;
	int16_t tempX = theBall.xPos;
	int16_t tempY = theBall.yPos;
	msg.xPos		= tempX&0xFF;
	msg.extraBits	= ((tempX & 0x0700)>>3)&0xE0;
	msg.yPos		= tempY&0xFF;
	msg.extraBits |= ((tempY & 0x0700)>>6)&0x1C;
	msg.extraBits |= theBall.id&0x03;
	msg.xVel = theBall.xVel;
	msg.yVel = theBall.yVel;
	msg.radius = (theBall.radius&0xFC) | ((theBall.id&0x0C)>>2);
	ir_send(ALL_DIRS, (char*)(&msg), sizeof(BallMsg));
	lastBallMsg=get_time();
}

void handleBallMsg(BallMsg* msg, uint32_t arrivalTime){
	BALL_DEBUG_PRINT("Got Ball! T: %lu\r\n\tPos: (%5.1f, %5.1f)   Vel: (%hd, %hd) | lastUpdate: %lu\r\n", get_time(), theBall.xPos, theBall.yPos, theBall.xVel, theBall.yVel, theBall.lastUpdate);
	int16_t highX = (int16_t)(((int8_t)(msg->extraBits))>>5);
	int16_t highY = (int16_t)((((int8_t)(msg->extraBits))<<3)>>5);
	int16_t tempX = (int16_t)((highX<<8) | ((uint16_t)(msg->xPos)));
	int16_t tempY = (int16_t)((highY<<8) | ((uint16_t)(msg->yPos)));
	uint8_t id = ((msg->extraBits)&0x03) | (((msg->radius)&0x03)<<2);
	if(id == 0x0F && theBall.id!=0x0F){
		lastBallID = theBall.id;
		set_rgb(255,0,0);
	}else if(id == lastBallID && theBall.id==0x0F){
		return; //this is from someone who hasn't realized the ball is dead, yet.
	}
	theBall.xPos = tempX;
	theBall.yPos = tempY;
	theBall.id = id;
	theBall.xVel = msg->xVel;
	theBall.yVel = msg->yVel;
	theBall.radius = ((msg->radius)&0xFC);
	theBall.lastUpdate = arrivalTime-4;
	BALL_DEBUG_PRINT("\tPos: (%5.1f, %5.1f)   Vel: (%hd, %hd) | lastUpdate: %lu\r\n", theBall.xPos, theBall.yPos, theBall.xVel, theBall.yVel, theBall.lastUpdate);
}

void sendNearBotsMsg(){ 
	NearBotsMsg msg;
	msg.flag = NEAR_BOTS_MSG_FLAG;
	msg.x    = myPos.x;
	msg.y    = myPos.y;
	msg.posConf = myPos.conf;
	for(uint8_t i=0;i<NUM_SHARED_BOTS;i++){
		msg.shared[i].id = nearBots[i].meas.id;		
		msg.shared[i].range = packRange(nearBots[i].meas.r);
		msg.shared[i].b = packAngleMeas(nearBots[i].meas.b);
		msg.shared[i].h = packAngleMeas(nearBots[i].meas.h);
		msg.shared[i].conf = nearBots[i].meas.conf;
	}
	ir_send(ALL_DIRS, (char*)(&msg), sizeof(NearBotsMsg));
}

void handleNearBotsMsg(NearBotsMsg* msg, id_t senderID){
	OtherBot* nearBot = getOtherBot(senderID);
	if(nearBot){
		NB_DEBUG_PRINT("(NearBotsMsg) ID: %04X", senderID);
		if(msg->x!=UNDF && msg->y!=UNDF)
			NB_DEBUG_PRINT("\tX: %4d Y: %4d", msg->x, msg->y);
		NB_DEBUG_PRINT("\r\n");
		(nearBot->pos).x = msg->x;
		(nearBot->pos).y = msg->y;
		(nearBot->pos).conf = msg->posConf;
		for(uint8_t i=0;i<NUM_SHARED_BOTS;i++){
			nearBot->shared[i].id = msg->shared[i].id;
			if(nearBot->shared[i].id == 0) continue;
			nearBot->shared[i].r = unpackRange(msg->shared[i].range);
			nearBot->shared[i].b = unpackAngleMeas(msg->shared[i].b);
			nearBot->shared[i].h = unpackAngleMeas(msg->shared[i].h);
			nearBot->shared[i].conf = msg->shared[i].conf;
			if(nearBot->shared[i].id == get_droplet_id())
				RNB_DEBUG_PRINT("\t(Shared->%04X)\tR: %4u B: %4d H: %4d | %4hd\r\n", senderID, nearBot->shared[i].r, nearBot->shared[i].b, nearBot->shared[i].h, nearBot->shared[i].conf);			
		}
	}
}

//void sendPaddleMsg(){
	//PaddleMsg msg;
	//switch(myState){
		//case CTRL_NE:
			//msg.flag = 'P';
			//msg.deltaPos = ((int16_t)paddleChange);
			//break;
		//case CTRL_NW:
			//msg.flag = 'P';
			//msg.deltaPos = -1*((int16_t)paddleChange);
			//break;
		//case CTRL_SE:
			//msg.flag = 'S';
			//msg.deltaPos = ((int16_t)paddleChange);
			//break;
		//case CTRL_SW:
			//msg.flag = 'S';
			//msg.deltaPos = -1*((int16_t)paddleChange);
			//break;
		//default:
			//paddleChange = 0.0;
			//return;
	//}
	//paddleChange = 0.0;
	//ir_send(ALL_DIRS, (char*)(&msg), sizeof(PaddleMsg));
	//printf_P(PSTR("Sent paddle msg with change: %d\r\n"), msg.deltaPos);
//}

//void handlePaddleMsg(char flag, int16_t delta){
	//if((myState==PIXEL_N && flag=='P') || (myState==PIXEL_S && flag=='S')){
		//paddleStart += delta;
		//paddleEnd += delta;
		//if(paddleStart<MIN_DIM){
			//paddleStart = MIN_DIM;
			//paddleEnd = MIN_DIM+ PADDLE_WIDTH;
		//}
		//if(paddleEnd>X_MAX_DIM){
			//paddleEnd = X_MAX_DIM;
			//paddleStart = X_MAX_DIM-PADDLE_WIDTH;
		//}
	//}
//}

void handle_msg(ir_msg* msg_struct){
	if(((BallMsg*)(msg_struct->msg))->flag==BALL_MSG_FLAG){
		handleBallMsg((BallMsg*)(msg_struct->msg), msg_struct->arrival_time);
	}else if(((NearBotsMsg*)(msg_struct->msg))->flag==NEAR_BOTS_MSG_FLAG){
		handleNearBotsMsg((NearBotsMsg*)(msg_struct->msg), msg_struct->sender_ID);
	//}else if(flag==N_PADDLE_MSG_FLAG || flag==S_PADDLE_MSG_FLAG){
		//handlePaddleMsg(flag, ((PaddleMsg*)(msg_struct->msg))->deltaPos);
	}else{
		printf_P(PSTR("%hu byte msg from %04X:\r\n\t"), msg_struct->length, msg_struct->sender_ID);
		for(uint8_t i=0;i<msg_struct->length;i++){
			printf("%02hX ", msg_struct->msg[i]);
		}
		printf("\r\n");
	}
}

void frameEndPrintout(){
	printf_P(PSTR("\nID: %04X T: %lu [ "), get_droplet_id(), get_time());
	switch(myState){
		case STATE_PIXEL:					printf("Pixel");		break;
		case (STATE_PIXEL|STATE_NORTH):		printf("North Pixel");	break;
		case (STATE_PIXEL|STATE_SOUTH):		printf("South Pixel");	break;
		//case CTRL_NE:		printf("NE Control");		break;
		//case CTRL_NW:		printf("NW Control");		break;
		//case CTRL_SE:		printf("SE Control");		break;
		//case CTRL_SW:		printf("SW Control");		break;
		//case CTRL_UNKNWN:	printf("?? Control");		break;
		default:							printf("???");			break;
	}
	printf_P(PSTR(" ]"));
	if(myPos.x != UNDF && myPos.y != UNDF){
		printf_P(PSTR("\tMy Pos: (%d, %d)"), myPos.x, myPos.y);
	}
	printf("\r\n");
	if(theBall.xPos != UNDF && theBall.yPos != UNDF){
		printf_P(PSTR("\tBall ID: %hu; radius: %hu; Pos: (%d, %d) @ vel (%hd, %hd)\r\n"), theBall.id, theBall.radius, theBall.xPos, theBall.yPos, theBall.xVel, theBall.yVel);
	}
	//if(NS_PIXEL(myState)){
		//printf_P(PSTR("\tPaddle: %d <-> %d.\r\n"), paddleStart, paddleEnd);
	//}
	printf("\r\n");	
}

OtherBot* getOtherBot(id_t id){
	for(uint8_t i=0;i<NUM_TRACKED_BOTS;i++){
		if(nearBots[i].meas.id==id){
			return &(nearBots[i]);
		}
	}
	return NULL;
}

void findAndRemoveOtherBot(id_t id){
	for(uint8_t i=0;i<NUM_TRACKED_BOTS;i++){
		if(nearBots[i].meas.id==id){
			cleanOtherBot(&nearBots[i]);
			numNearBots--;
			break;
		}
	}
}

void removeOtherBot(uint8_t idx){
	cleanOtherBot(&nearBots[idx]);
	numNearBots--;
}

OtherBot* addOtherBot(id_t id, int8_t conf){
	uint8_t emptyIdx=0xFF;
	qsort(nearBots, NUM_TRACKED_BOTS, sizeof(OtherBot), nearBotsConfCmpFunc);
	for(uint8_t i=0;i<NUM_TRACKED_BOTS;i++){
		if(nearBots[i].meas.id==id){
			return &(nearBots[i]);
		}
		if(emptyIdx==0xFF && nearBots[i].meas.id==0){
			emptyIdx=i;
		}
	}
	//The ID we're adding wasn't in our neighbors list.
	if(emptyIdx!=0xFF){ //An empty spot was available.
		numNearBots++;
		return &(nearBots[emptyIdx]);
	}
	// No empty spot, so we'll remove the neighbor we're
	// least confident in. But only if we're more confident
	// in the new neighbor.
	//BotPos* pos = &(nearBots[NUM_TRACKED_BOTS-1].pos);
	if(nearBots[NUM_TRACKED_BOTS-1].meas.conf<conf){
		POS_DEBUG_PRINT("No empty spot, but higher conf.\r\n");
		cleanOtherBot(&nearBots[NUM_TRACKED_BOTS-1]);
		return &(nearBots[NUM_TRACKED_BOTS-1]);
	}else{
		POS_DEBUG_PRINT("No empty spot, and conf too low.\r\n");
		return NULL;
	}
}

void cleanOtherBot(OtherBot* other){
	if(other==NULL) return;
	other->pos.x = UNDF;
	other->pos.y = UNDF;
	other->meas.id = 0;
	other->meas.r = UNDF;
	other->meas.b = UNDF;
	other->meas.h = UNDF;
	other->meas.conf = 0;
}

/*
 *	The function below is optional - commenting it in can be useful for debugging if you want to query
 *	user variables over a serial connection.
 */
uint8_t user_handle_command(char* command_word, char* command_args){	
	if(strcmp_P(command_word,PSTR("ball"))==0){
		if(UNDF!=myPos.x && UNDF!=myPos.y){
			const char delim[2] = " ";
			char* token = strtok(command_args, delim);
			int8_t vel = (token!=NULL) ? (int8_t)atoi(token) : 10;
			token = strtok(NULL, delim);
			uint8_t size = (token!=NULL) ? (uint8_t)atoi(token) : 60;
			theBall.xPos = myPos.x;
			theBall.yPos = myPos.y;
			int16_t randomDir = rand_short()%360;
			theBall.xVel = vel*cos(deg_to_rad(randomDir));
			theBall.yVel = vel*sin(deg_to_rad(randomDir));
			theBall.id = (lastBallID+1)%(0x0E);
			theBall.radius = size&0xFC;
			theBall.lastUpdate = get_time();
			printf_P(PSTR("Got ball command. Velocity: %hd\r\n"), vel);
		}else{
			uint8_t r = get_red_led();
			set_red_led(255);
			printf_P(PSTR("Got ball command, but I don't know where I am yet.\r\n"));
			set_red_led(r);
		}
		return 1;
	}else if(strcmp_P(command_word,PSTR("mode"))==0){
		switch(command_args[0]){
			case 'p': colorMode = POS;			break;
			case 's': colorMode = SYNC_TEST;	break;
			case 'o': colorMode = OFF;			break;
		}
		switch(command_args[1]){
			case 'p': gameMode = PONG;			break;
			case 'b': gameMode = BOUNCE;		break;
			default:  gameMode = BOUNCE;		break;
		}
		return 1;
	}else if(strcmp_P(command_word,PSTR("ball_kill"))==0){
		killBall();
		return 1;
	}
	return 0;
}


void addHardBot(id_t id){
	if(hardBotsList==NULL){
		ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
			hardBotsList = (HardBot*)malloc(sizeof(HardBot));
		}
		hardBotsList->id = id;
		hardBotsList->next = NULL;
		}else{
		HardBot* temp = hardBotsList;
		while(temp->next!=NULL){
			if(temp->id==id) return; //requested ID is already added.
			temp = temp->next;
		}
		ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
			temp->next = (HardBot*)malloc(sizeof(HardBot));
		}
		temp->next->id = id;
		temp->next->next = NULL;
	}
}

void cleanHardBots(){
	HardBot* temp;
	while(hardBotsList!=NULL){
		temp = hardBotsList->next;
		ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
			free(hardBotsList);
		}
		hardBotsList = temp;
	}
}