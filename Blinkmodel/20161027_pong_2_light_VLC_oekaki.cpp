//
// Light crayon with new VLC. 
// blue and green is used to determine ID
//
//  Blue  Green   ID
//   OFF   ON     0
//   ON    OFF    1
//   ON    ON     2
//
// max 3 users

#define _USE_MATH_DEFINES

#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <random>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <Windows.h>
#include <MMsystem.h>

using namespace std;
using namespace cv;

const float  FrameWidth = 720;							//カメラからの画像のX  IEEEカメラなら720*480
const float  FrameHeight = 480;							//カメラからの画像のY  USBカメラなら 1280*720
const float  DispFrameWidth = 800;						//dispの横
const float  DispFrameHeight = 600;						//dispの縦
int findLightSpan = 30;									//LED探索ルーチンを行うフレーム間隔
int GreyThreshold = 83;								//2値化のスレッショルド
int LightSpaceThreshold = 100;							//光源かノイズかの閾値
int LightMoveThreshold = 70;							//フレームごとに移動した光源の距離がこれより下ならば同一の光源と見る
int lightSearchFrameSpan = 50;							//画面上から新しい光源をスキャンするときの最小ピクセル幅
const int  LightMax = 4;								//最大利用可能人数
const int  BinDataLong = 5;								//バイナリデータのビット長
int  TtdLifetime = 30;									//Time to death　LEDが見つからなかったときに増えるttdがこれ以上のとき、LEDは消失したと考える
const int MaxAllowedIdMismatch = 5;						//IDによるエラー検知の最大回数　これ以上ならばPointをkillする
int ContourThickness = 1;								//輪郭線の太さ
int  LineThickness = 10;								//線幅
int RedThreshold = 99;									//赤色の閾値
int BlueThreshold = 120;									//青色の閾値
int greenThreshold = 120;
const float  DifDisplayX = DispFrameWidth / FrameWidth;		// dispFrame/Frame カメラ画像からウインドウへの座標変換 xy座標にこれをかけるとdispでの座標になる
const float  DifDisplayY = DispFrameHeight / FrameHeight;
const string windowNameDisp = "Disp";
const string windowNameThreshold2 = "Threshold2";

vector<Scalar> penColor = { Scalar(0, 0, 0), Scalar(150, 0, 0), Scalar(0, 150, 0), Scalar(0, 0, 150), Scalar(150, 150, 0) };			//パネルの変化する色　ID=0は黒
vector<Scalar> idColor = { Scalar(0, 150, 150), Scalar(150, 0, 0), Scalar(0, 150, 0), Scalar(0, 0, 150), Scalar(150, 150, 0) };			//IDの色

Mat rawCamera;												//カメラ
Mat Thresholded2;											//rawcameraの表示用コピー
Mat disp(Size(static_cast<int>(DispFrameWidth), static_cast<int>(DispFrameHeight)), CV_8UC3, Scalar(0, 0, 0));			//メイン画面　
Mat disp2(Size(DispFrameWidth, DispFrameHeight), CV_8UC3, Scalar(0, 0, 0));			//カーソルと合成されて実際に表示される方
Mat backGround(Size(DispFrameWidth, DispFrameHeight), CV_8UC3, Scalar(0, 0, 0));	//背景画像を保存するMAT
Mat edgeImage(Size(DispFrameWidth, DispFrameHeight), CV_8UC1, Scalar(0));			//エッジ検出されたイメージ
vector<Mat> colorSplitDisp;															//閾値調節用のBGR各色の成分のみを抜き出したMAT

//パネル用パラメータ
int const panelStat = LightMax+1;									//パネルがとりえる状態の総数 すべてのIDの数 + 黒（ID未対応）
int const panelDefaultX = 10, panelDefaultY = 10;							//x,yそれぞれのパネル数
float panelDetectionRate = 0.8f;								//パネルの中心からこの値の比率だけの範囲で点が検出されたときにpanelのpDatを行進する　0.5ならば パネル単体のx軸長さ　*0.25 -0.75の範囲

//for Ball
random_device rndBallDic;					//ボールの方向生産用
int maxBallSpeed = 64;						//ボールの最高速度
int defaultBallR = 20;						//ボールのデフォルト半径
Scalar defaultBallCol = Scalar(0, 255, 0);	//ボールのデフォルト色緑

//for PlayerBar
const int lineNum = 10;							//ひとつのPlayerBarを構成する線数
const int lineThickness = 5;
const int PlayerBarRenewInterbal = 3;	
const int maxLineLength = 30;

//for pong
const String fieldImage2Player = "pongField2Player.png";
const String fieldImage3Player = "pongField3Player.png";
const int maxBall = 1;
const Point pointDisplayPos2Player(250, 80);		//二人プレイ時のポイント表示の場所
const Point pointDisplayPos3Player[3] = { Point(720, 560), Point(720, 100), Point(10, 330)}; //三人プレイ時のポイント表示位置
const int showPointScale = 3;
const int pointDispTime = 90;		//30hz 3s
const int maxContinuousBallreflect = 5;				//ボールがこの回数以上連続してバーと衝突しているならボールがバーで囲まれて動けなくなっていると見て一時的に反射を無効とする
int pongCnt = 0;					//pong用カウンタ変数
int winConditionPoint = 10;			//勝利に必要な点数

struct collisionList{				//checkCollideCircleField用
	int id=0;							//フィールドと衝突したボールのid
	double angle=0;					//衝突したときの中心から見たラジアン
};
struct ballBarCollisionList{
	int ballId=0, barId=0;			//ボールとバーのid
	Point2f vecOrigin;				//反射ベクトルの原点
	Point2f vec;						//反射ベクトルのベクトル
};
class PointerData{							//画面に表示されるLED光点を管理するクラス

	private:

		int x, y, bin, l, buf,ttd;			//x axis, y axis , binary data, data length, LED創作ルーチンでLEDが発見され、binが更新されたか , buf:前回のbinを保存, ttd:消灯してからPointからさ駆除されるまでの時間
		int allowedIdMis;					//IDエラー検地によるエラーの許される回数
		bool alive, work;					//alive:LEDがデータ転送中か work:画面内にLEDが存在するか　0:存在しない　1:存在し、IDが決定されている　4-2:存在するがIDは決定中　
		int id;								//光クレヨンのid -1で未確定
		int color;							//カラーID　00:消しゴム　01:赤 10:青　11:緑
		bool cur;							//カーソル状態 true:押されている
		int lx, ly;							//直前のxy座標
		vector<int> decidedat;				//idを決定するために3回IDを読み込み、多数決をとる　
		string debugdat;					//過去のbinを蓄積
		int barIndex;						//リングバッファのインデックス
	public:
		PointerData(){
			this->x = 0;
			this->y = 0;
			this->bin = 0;
			this->l = 0;
			this->buf = 0;
			this->ttd = 0;
			this->allowedIdMis = 0;
			this->debugdat="";
			this->alive = false;
			this->work = false;
			this->id = -1;
			this->color = -1;
			this->cur = false;
			this->lx = 0;
			this->ly = 0;
			this->barIndex = 0;
		}
		void newPoint(int x, int y){
			this->x = x;
			this->y = y;
			this->lx = 0;
			this->ly = 0;
			this->alive = true;
		}
		void killPoint(){
			this->x = 0;
			this->y = 0;
			this->bin = 0;
			this->l = 0;
			this->buf = 0;
			this->ttd = 0;
			this->allowedIdMis = 0;
			this->debugdat = "";
			
			this->alive = false;
			this->work = false;
			this->id = -1;
			this->color = -1;
			this -> decidedat.clear();
		
		}
		int getX(){
			return x;
		}
		int getY(){;
			return y;
		}
		int getBin(){
			return buf;
		}
		int getLength(){
			return l;
		}
		int addToBin(int dat){					//binにデータ1,0を入れる 返り値: 0:正常　1:ttd上限のためポイント消失　2:MaxAllowedIdMismatchの上限のためポイント消去

			if (dat == 1){		//デバッグ情報
				debugdat.insert(0,"1");
			}
			else{
				debugdat.insert(0,"0");
			}

			if (rawCamera.at<Vec3b>(this->y, this->x)[0] > BlueThreshold){		//カーソル（青色）の検知
				this->cur = true;
			}
			else{
				this->cur = false;
			}

			if (dat == 0){
				ttd++;	//消灯ならばttdをインクリメント
			}
			else{
				ttd = 0;
			}

			if (work == false){
				if (dat == 1){
					work = true;		//aliveがfalseかつ入力が1ならば受付状態とする
				}
			}else{						//alive=trueつまり受付中
				bin = (bin << 1) + dat;
				l++;
				if (l > BinDataLong-1){			//データ長lが全データであるBinDataLongつまりすべてのデータを受信し終えたときの処理
					work = false;
					buf = bin;
					bin = 0;
					l = 0;
					setIdColor();		
				}
			}
			
			if (ttd > TtdLifetime){		//ttdが式一以上ならば、LEDを殺す
				killPoint();
				return 1;
			}
			/*
			if (allowedIdMis > MaxAllowedIdMismatch){	//IDによるエラーを指定回数連続で検知されたらPointを殺す
				killPoint();
			}
			*/
			return 0;		//
		}
		void setXY(int x, int y){	
			this->lx = this->x;
			this->ly = this->y;
			this->x = x;
			this->y = y;
			if (this->x < 0) this->x = 0;
			if (this->x > FrameWidth-1) this->x = FrameWidth-1;		//x=720のピクセルは存在しない
			if (this->y < 0) this->y = 0;
			if (this->y > FrameHeight-1) this->y = FrameHeight-1;
		}
		bool getAlive(){
			return alive;
		}
		bool getWork(){
			return work;
		}
		int getTTD(){
			return ttd;
		}
		std::string debug(){
			return debugdat;
		}
		void setIdColor(){
			int parity = ((this->buf) & 1) + (((this->buf) & 2) >> 1) + (((this->buf) & 4) >> 2) + (((this->buf) & 8) >> 3) + (((this->buf) & 16) >> 4);
			if ((parity % 2) == 0){								//偶数のパリティビットのときのみ値を読み込む
				if (id == -1) id = (this->buf >> 3);			//上位2bitをidとする
				if (id == (this->buf >> 3)){					//読み取れたIDはPoint.idと等しいか？
					this->color = (this->buf >> 1) & 3;			//下位2bitを色番号とする
					this->allowedIdMis = 0;
				}
				else{
					allowedIdMis++;					//IDによるエラー検知
					cout << "tick=" << to_string(GetTickCount()) << ":point[" << to_string(id) << "]parity bit mismatch" << endl;
				}
			}
		}
		int getId(){
			return this->id;
		}
		int getColor(){
			return this->color;
		}
		bool getCur(){
			return cur;
		}
		void drawLine(cv::Mat& dispInp){
			switch (this->color){
			case 1:
				line(dispInp, Point(static_cast<int>(DifDisplayX*this->lx), static_cast<int>(DifDisplayY*this->ly)), Point(static_cast<int>(DifDisplayX*this->x), static_cast<int>(DifDisplayY*this->y)), Scalar(0, 0, 255), LineThickness, 4, 0);
				break;
			case 2:
				line(dispInp, Point(static_cast<int>(DifDisplayX*this->lx), static_cast<int>(DifDisplayY*this->ly)), Point(static_cast<int>(DifDisplayX*this->x), static_cast<int>(DifDisplayY*this->y)), Scalar(255, 0, 0), LineThickness, 4, 0);
				break;
			case 3:
				line(dispInp, Point(static_cast<int>(DifDisplayX*this->lx), static_cast<int>(DifDisplayY*this->ly)), Point(static_cast<int>(DifDisplayX*this->x), static_cast<int>(DifDisplayY*this->y)), Scalar(0, 255, 0), LineThickness, 4, 0);
				break;
			default:
				//line(dispInp, Point(static_cast<int>(DifDisplayX*this->lx), static_cast<int>(DifDisplayY*this->ly)), Point(static_cast<int>(DifDisplayX*this->x), static_cast<int>(DifDisplayY*this->y)), Scalar(0, 0, 0), LineThickness, 4, 0);
				break;
			}
		}
		void drawContourLine(){		//hough変換用の輪郭イメージを表示する
			if (!cur) return;
			line(edgeImage, Point(static_cast<int>(DifDisplayX*this->lx), static_cast<int>(DifDisplayY*this->ly)), Point(static_cast<int>(DifDisplayX*this->x), static_cast<int>(DifDisplayY*this->y)), Scalar(255), ContourThickness, 4, 0);	
		}

		
		void drawCursor(Mat& dispInp){
			//cout << "this->x=" << to_string(DifDisplayX*this->x) << ",this->y=" << to_string(DifDisplayY*this->y) << endl;
			circle(dispInp, Point(DifDisplayX*this->x, DifDisplayY*this->y), LineThickness + 1, Scalar(255, 255, 255), 2, 4, 0);
			/*
			if (id != -1){
				try{
					putText(disp2, to_string(id), Point(DifDisplayX*this->x, DifDisplayY*this->y), FONT_HERSHEY_COMPLEX, 0.5, Scalar(200, 200, 200));
				}
				catch (cv::Exception exp){ cout << "cv::exception" << endl; }
			}
			*/
			if (!this->cur){
				switch (this->color){
				case 1:
					circle(dispInp, Point(static_cast<int>(DifDisplayX*this->x), static_cast<int>(DifDisplayY*this->y)), LineThickness, Scalar(0, 0, 255), -1, 4, 0);
					break;
				case 2:
					circle(dispInp, Point(static_cast<int>(DifDisplayX*this->x), static_cast<int>(DifDisplayY*this->y)), LineThickness, Scalar(255, 0, 0), -1, 4, 0);
					break;
				case 3:
					circle(dispInp, Point(static_cast<int>(DifDisplayX*this->x), static_cast<int>(DifDisplayY*this->y)), LineThickness, Scalar(0, 255, 0), -1, 4, 0);
					break;
				default:
					circle(dispInp, Point(static_cast<int>(DifDisplayX*this->x), static_cast<int>(DifDisplayY*this->y)), LineThickness, Scalar(0, 0, 0), -1, 4, 0);
					break;

				}
			}
		}
		void setCur(bool in){
			this->cur = in;
		}
		void setId(int in){
			this->id = in;
		}
};
class Panel{
private:
	int panelX, panelY;		//x,yそれぞれの方向のパネル枚数
	int pStatNum;			//panelDat
	int pDat[panelDefaultX][panelDefaultY];				//核パネルの状態
	vector<Scalar> pColor;	//panelDatの各状態に対応する色
public:
	Panel(){				//コンストラクタ px,pyはxyそれぞれのパネル数
		panelX = panelDefaultX;
		panelY = panelDefaultY;
		pStatNum = panelStat;
		for (int i=0; i < panelDefaultX - 1; i++){
			for (int j=0;j < panelDefaultY - 1; j++){
				pDat[i][j] = 0;
			}
		}
		pColor = penColor;
	}
/*			パネルのxy軸ごとの枚数を引数にしたコンストラクタを作りたいけど、int[][]の宣言の仕方がわからん　後回し
	Panel(int px,int py):panelX(px),panelY(py){				//コンストラクタ px,pyはxyそれぞれのパネル数
		pStatNum = panelStat;
		for (int i = 0; i < panelDefaultX - 1; i++){
			for (int j = 0; j < panelDefaultY - 1; j++){
				pDat[i][j] = 0;
			}
		}
		pColor = panelColor;
	}
*/
	void set(int px,int py, int stat){	//パネル座標(x,y)にstatを設定
		pDat[px][py] = stat+1;
	}
	void setByScrPos(int px, int py,int stat,const Mat& target){	//スクリーン上の座標で入力できるset(),描画したいディスプレイの大きさを取得するためにMatを指定
		float pXnum, pYnum;		//描画するパネルのxyそれぞれの数
		float pWidth, pHeight;	//描画するパネルのxyそれぞれの幅
		float pXnumOffset, pYnumOffset;	//パネルの原点からどれだけ離れているか
		pXnum = static_cast<float>(target.cols);
		pYnum = static_cast<float>(target.rows);
		pWidth = pXnum / this->panelX;
		pHeight = pYnum / this->panelY;
		pXnumOffset = px % static_cast<int>(pWidth);		//0 <= pXnumOffset <= panelXのはず 
		pYnumOffset = py % static_cast<int>(pHeight);
		//cout << "px/pWidth,py/PHeight)=( " << floor(px / pWidth) << "," << floor(py / pHeight) << ")(pXnumOffset,pYnumOffset)=(" << to_string(pXnumOffset) << "," << to_string(pYnumOffset) << ")" << endl;
		if ((pXnumOffset > panelDetectionRate / 2 * pWidth) && ((panelDetectionRate / 2 + 0.5)*pWidth > pXnumOffset) && (pYnumOffset > panelDetectionRate / 2 * pHeight) && (panelDetectionRate / 2 + 0.5)*pHeight > pYnumOffset){
			set(floor(px / pWidth*DifDisplayX), floor(py / pHeight*DifDisplayY), stat);
			
		}
	}
	int get(int px,int py){			//パネル(x,y)を取得
		return pDat[px][py];
	}
	void drawPanel(Mat& target){
		float pXnum, pYnum;		//描画するパネルのxyそれぞれの数
		float pWidth, pHeight;	//描画するパネルのxyそれぞれの幅
		int stat_buf;
		pXnum = static_cast<float>(target.cols);
		pYnum = static_cast<float>(target.rows);
		pWidth = pXnum / this->panelX;
		pHeight = pYnum / this->panelY;
		for (int x = 0; x < panelX-1; x++){		//パネルをrectangleで描画
			for (int y = 0; y < panelY-1; y++){	
				//cout << "x,y=(" << to_string(x) << "," << to_string(y) << ") stat=" << to_string(pDat[x][y]) << endl;
				stat_buf = pDat[x][y];
				if (stat_buf != 0){
					rectangle(target, cvRect(static_cast<int>(x*pWidth), static_cast<int>(y*pHeight), static_cast<int>(pWidth), static_cast<int>(pHeight)), pColor[stat_buf], -1);
				}
			}
		}
	}

};
class Ball{
private:
	float x,y,r;	//x座標,y座標,半径
	float lx, ly;	//
	float ax, ay;	//x,y軸への移動量
	Scalar col;		//色
	int stat,id;	//1-なら有効 id:直前に衝突したバーのid
	int refNum;		//連続して反射処理したフレーム数
	
public:
	bool refR, refL, refU, refD;	//反射したなら1

	Ball(){
		x = static_cast<int>(DispFrameWidth / 2);
		y = static_cast<int>(DispFrameHeight / 2);
		r = defaultBallR;
		ax = 4;
		ay = 0;
		col = defaultBallCol;
		stat = 0;
		refNum = 0;
		refR = false;
		refL = false;
		refU = false;
		refD = false;
		id = 0;
	}
	Ball(cv::Scalar ballcol) 
		:x(static_cast<int>(DispFrameWidth / 2)), y(static_cast<int>(DispFrameHeight / 2)), r(defaultBallR), ax(-3), ay(-5),
		col(ballcol), stat(0), refR(false), refL(false), refU(false), refD(false), id(0) {}

	void activate(){
		stat = 1;
	}
	void deactivate(){
		stat = 0;
	}
	void setDafault(){
		x = static_cast<int>(DispFrameWidth / 2);
		y = static_cast<int>(DispFrameHeight / 2);
	}
	void move(){		//ax,ayだけボールを動かす
		//cout << "ax:" << ax << "ay:" << ay << endl;
		refR = false;
		refL = false;
		refU = false;
		refD = false;
		int tx=lx, ty=ly;		//直前のxy
		lx = x, ly = y;
		x = x + ax;
		y = y + ay;
		if ((x - r < 0)){		//x軸の画面端衝突
			x = lx;
			lx = tx;
			ax = -ax;
			refL = true;
		} 
		if (x + r > DispFrameWidth){	//右衝突
			x = lx;
			lx = tx;
			ax = -ax;
			refR = true;
		}
		if ((y - r < 0)){		//y軸の画面短衝突
			y = ly;
			ly = ty;
			ay = -ay;
			refU = true;
		}
		if (y + r > DispFrameHeight){
			y = ly;
			ly = ty;
			ay = -ay;
			refD = true;
		}
	}
	void setAccel(float pax,float pay){
		ax = pax;
		ay = pay;
	}
	void setAccelX(float pax){
		ax = pax;
	}
	void setAccelY(float pay){
		ay = pay;
	}
	void setPos(int px, int py){
		x = px;
		y = py;
	}
	void setColor(Scalar inpCol){
		col = inpCol;
	}
	void incRefNum(){ refNum++; }
	void clearRefNum() { refNum = 0; }
	float getX(){
		return x;
	}
	float getY(){
		return y;
	}
	float getR(){
		return r;
	}
	float getAX(){
		return ax;
	}
	float getAY(){
		return ay;
	}
	int getId(){ return id; }
	int getRefNum(){ return refNum; }
	float getLx(){ return lx; }
	float getLy() { return ly; }
	void setId(int inp) { id = inp; }
	void draw(Mat dest){
		if (stat == 1) {
			circle(dest, Point(x, y), r, col,-1);
		}
	}
};
class PlayerBar{
private:
	int ringBufIndex;	//Barを格納する配列のリングバッファ的先頭の要素
	float barArray[lineNum+1][2];	//バーの各頂点を格納する配列　リングバッファ
	Scalar color;		//バーの色　idColor参照
	int stat,point;		
						//idが必要,,受信したidに従って線の色を変えるために
public:
	PlayerBar(){
		color = Scalar(0,255,0);
		for (int i = 0; i < lineNum + 1; i++){
			barArray[i][0] = 0;
			barArray[i][1] = 0;
		}
		point = 0;
	}
	PlayerBar(Scalar plyCol){
		ringBufIndex = 0;
		color = plyCol;
		stat = 0;
		point = 0;
		for (int i = 0; i < lineNum + 1; i++){
			barArray[i][0] = 0;
			barArray[i][1] = 0;
		}
	}
	void activate(){
		stat = 1;
	}
	void deactivate(){
		stat = 0;
		for (int i = 0; i < lineNum + 1; i++){
			barArray[i][0] = 0;
			barArray[i][1] = 0;
		}
	}
	void addBar(int px, int py){		//頂点を追加
		barArray[ringBufIndex][0] = DifDisplayX*px;
		barArray[ringBufIndex][1] = DifDisplayY*py;
		ringBufIndex = (ringBufIndex + 1) % (lineNum + 1);		//リングバッファを進める
	}
	void addSlowBar(int px, int py){		//頂点を追加

		int prvInd = (ringBufIndex - 1) % (lineNum + 1);
		int apx = barArray[prvInd][0], apy = barArray[prvInd][1];		//ひとつ前の頂点座標
		int s2;															//長さの二乗
		if (apx *apy != 0){		//前の座標が初期値ではないか
			s2 = ((px - apx) ^ 2 + (py - apy) ^ 2);
			if (s2 < maxLineLength^2){
				barArray[ringBufIndex][0] = px;
				barArray[ringBufIndex][1] = py;
			}
			else{		//maxLineLength以上の線を描かない
				barArray[ringBufIndex][0] = maxLineLength*(px-apx) / sqrt(s2) + apx;
				barArray[ringBufIndex][1] = maxLineLength*(py-apy) / sqrt(s2) + apy;			//古い点から新しい点へのベクトルを正規化して、maxLIneLengthの長さのベクトルを生成
			}
		}
		ringBufIndex = (ringBufIndex + 1) % (lineNum + 1);		//リングバッファを進める
	}
	void draw(Mat dest){
		if (stat = 1){
			for (int i = 0; i < lineNum + 0; i++){
				float ax = barArray[(i + ringBufIndex) % (lineNum + 1)][0], ay = barArray[(i + ringBufIndex) % (lineNum + 1)][1], bx = barArray[(i + ringBufIndex + 1) % (lineNum + 1)][0], by = barArray[(i + ringBufIndex + 1) % (lineNum + 1)][1];
				if (ax*ay*bx*by!=0) line(dest, Point(ax, ay), Point(bx,by ), color,LineThickness); //座標が0の頂点を持つ線は描かない
			}
		}
	}
	bool getCollideBallPos(Ball& obj,Point2f& poi,Point2f& newpos){		//ballと衝突しているか,衝突していたら反射ベクトルを返す obj:ボールを示す　poi: newpos:ballがbarにめりこみを直した後のballの座標 http://marupeke296.com/COL_2D_No5_PolygonToCircle.html
		float ballx=obj.getX(),bally=obj.getY(), ballr=obj.getR();
		float ary1x, ary1y, ary2x, ary2y;
		float sx, sy, ax, ay,bx,by,absSA,absS,d,dotas,dotbs,sbr;			//d=|S×A|/|S| Sは終点-始点
		float nx, ny,nabs;													//衝突線の正規化法線ベクトル
		float fx=obj.getAX(), fy=obj.getAY();								//衝突時のベクトル
		float a;
		float rx, ry;
		bool ret=false;														//返り値
		for (int i = 0; i < lineNum + 0; i++){
			ary2x = barArray[(i + ringBufIndex + 1) % (lineNum + 1)][0];
			ary2y = barArray[(i + ringBufIndex + 1) % (lineNum + 1)][1];
			ary1x = barArray[(i + ringBufIndex) % (lineNum + 1)][0];
			ary1y = barArray[(i + ringBufIndex) % (lineNum + 1)][1];
			if ((ary1x*ary2x == 0) || (ary2x*ary2y == 0)) continue;  //座標に(0,0)を含む場合計算に入れない
			sx = ary2x - ary1x;								//S =ary2-ary1
			sy = ary2y - ary1y;
			ax = ballx - ary1x;
			ay = bally - ary1y;
			absSA = abs(sx*ay-ax*sy);
			absS = sqrt(sx*sx + sy*sy);
			if (absS == 0) continue;
			d = absSA / absS;
			//cout << d << endl;
			if (d > ballr){
				continue;
			}
			else{
				bx = ballx - barArray[(i + ringBufIndex + 1 ) % (lineNum + 1)][0];
				by = bally - barArray[(i + ringBufIndex + 1 ) % (lineNum + 1)][1];
				if ((ax*sx + ay*sy)*(bx*sx + by*sy) > 0){
					continue;
				}
				else{			//衝突　反射ベクトルを求める    n=S×A=(sx,sy,0)×(sx,sy,1)=(sy,-sx,0)
					nabs = sqrt(sy*sy + sx*sx);			//nの絶対値
					nx = sy / nabs;
					ny = -sx / nabs;
					//r=f-2dot(f,n)*n
					//a=2(fxnx+fyny)
					a = 2*(fx*nx + fy*ny);
					rx = fx - a*nx;
					ry = fy - a*ny;
					if (rx*rx + ry*ry > maxBallSpeed*maxBallSpeed){			//ベクトルの最大速度制限　maxBallSpeedまで
						float sum_r = sqrt(rx*rx + ry*ry);
						rx = rx / sum_r*maxBallSpeed;
						ry = ry / sum_r*maxBallSpeed;
					}
					//めり込んだ円を線からはずす　　直線Sの法線は (Sx,Sy,0)×(0,0,1)=(-sy,sx,0)
					//この法線を正規化するとnx=1/√(sx^2+sy^2)*-sy, ny=1/√(sx^2+sy^2)*sx
					//Sに対して,Sの根元から(bx,by)へのベクトルが右にあるが左にあるかは,(Sx,sy)×(BX,BY)>0ならn*+ <0 ならn*-
					//
					sbr = sx*bally - sy*ballx;
					newpos = Point2f(obj.getLx(), obj.getLy());			//衝突する直前の座標を返す
					/*
					if (sbr > 0){
						newpos = Point2f((ballr - d)*nx+ballx, (ballr - d)*ny+bally);
						cout << "sbr=+" << endl;
					}
					else{
						newpos = Point2f((ballr - d)*-nx+ballx, (ballr - d)*-ny+bally);
						cout << "sbr=-" << endl;
					}
					*/
					poi=Point2f(rx, ry);
					
					cout << "d=" << d << ":from(" << fx << "," << fy << ") to (" << fx - a*nx << "," << fy - a*ny << ")" << endl;
					cout << "pos:(" << ballx << "," << bally << ") to (" << ballx + newpos.x << "," << bally + newpos.y << ")" << endl;
					cout << "----------------------------------------------------------------------------------------------------------" << endl;
					return true;
				}
			}
		}
		return false;
	}
	void addOneRingBuf(){			//リングバッファのインデックスを1進める
		ringBufIndex = (ringBufIndex + 1) % (lineNum + 1);		//リングバッファを進める
	}
	void addPoint(int ip){
		point = point + ip;
		if (point < 0) point = 0;
	}
	int getPoint(){
		return point;
	}
	void setPoint(int ip){
		this->point = ip;
		cout << "point" << to_string(this->point) << endl;;
	}	
	void setColor(Scalar colInp){ color = colInp; }
	void reset(){
		for (int i = 0; i < lineNum + 1; i++){
			barArray[i][0] = 0;
			barArray[i][1] = 0;
		}
	}
};
class Pong{
private:
	vector<Ball> b;			//ボールの数
	vector<PlayerBar> p;		//プレイヤー数	
	int stat, playerNum, ballNum;	//playerNum:プレイ人数 ballNum:ボール数
public:
	Pong(int ply, int mball) :playerNum(ply), ballNum(mball), stat(0){
		for (int i = 0; i < playerNum; i++) p.push_back(PlayerBar(idColor[i]));	//vector<Ball>の宣言
		for (int i = 0; i < ballNum; i++) b.push_back(Ball());
	}
	~Pong(){}
	void startGame(){	//ゲームを始める
		//ボールを生産
		for (int i = 0; i < ballNum; i++){
			b[i].activate();
		}
		//ゲームバーを生産
		for (int i = 0; i < playerNum; i++){
			p[i].activate();
		}
		stat = 1;		//ゲーム中
	}
	void endGame(){
		//ボールを非活性化
		for (int i = 0; i < ballNum; i++){
			b[i].deactivate();
		}
		//ゲームバーを非活性化
		for (int i = 0; i < playerNum; i++){
			p[i].deactivate();
		}
		stat = 0;
		clearAllPoint();
	}
	void moveBalls(){								//ボールをすべて動かす
		if (stat == 1){
			for (int i = 0; i < ballNum; i++){
				b[i].move();
			}
		}
	}
	void updateBars(vector<PointerData>& source, bool isSlow){		//Point Source[LightMax]を元にすべてのプレイヤーバー(Pong::p[])を更新
		int PointDatId = 0;
		for (int i = 0; i < playerNum; i++){
			float srcx = source[i].getX(), srcy = source[i].getY();		//バーの新しい座標を取得
			if ((srcx != 0) || (srcy != 0)){							//座標が(0,0)でないか
				cout << "i=" << to_string(i) << endl;
				cout << "source.size()=" << to_string(source.size()) << endl;
				cout << "p.size()=" << to_string(p.size()) << endl;
				cout << "source[i].getId()=" << to_string(source[i].getId()) << endl;
				PointDatId = source[i].getId();
				if (PointDatId != -1){
					p[i].setColor(idColor[source[i].getId()]);
					if (isSlow){
						cout << "isSlow=true" << endl;
						p[i].addSlowBar(srcx, srcy);
					}
					else{
						p[i].addBar(srcx, srcy);
						cout << "isSlow=false" << endl;
					}
				}
			}
			else{
				p[i].addOneRingBuf();
			}
		}
	}

	vector<ballBarCollisionList> checkPlayerBallCollide(){					//すべてのバーでボールとの衝突を判定し、衝突していたら反射処理 vector<vector<int>>{衝突したBarのid,衝突したBallのid}を返す
		Point2f poi, newpos, correctedPos;
		vector<ballBarCollisionList> listCollide;
		ballBarCollisionList temp;
		for (int j = 0; j < ballNum; j++){
			bool isRefrectinFrame = false;									//このフレームでこのボールは反射処理を行ったか？
			for (int i = 0; i < playerNum; i++){			
				if (p[i].getCollideBallPos(b[j], poi, newpos)){
					cout << "collide:ball[" << to_string(j) << "]" << endl;
					isRefrectinFrame = true;									//このボールはこのフレームで反射を行った
					if (b[j].getRefNum()<maxContinuousBallreflect){				//maxContinuousBallrefrectの回数だけ連続して反射処理をしていなければ
						b[j].incRefNum();										//refNumをインクリメント
						b[j].setAccel(poi.x, poi.y);
						b[j].setPos(newpos.x, newpos.y);
						temp.ballId = j;
						temp.barId = i;
						temp.vecOrigin = correctedPos;
						temp.vec = Point(newpos.x, newpos.y);
						listCollide.push_back(temp);
						b[j].setId(i);							//ボールに直前に衝突したプレイヤーidを入力
					}
					else{
						cout << "ball " << to_string(j) << " skiped refrect precedure due to max refNum" << endl;
					}
				}
				else{

				}
				
			}
			if (isRefrectinFrame == true){ b[j].incRefNum(); }
			else { b[j].clearRefNum(); }
		}
		return listCollide;
	}
	void changeAllBallColor(vector<ballBarCollisionList>& listCollide){ //correctCollide()から帰ってきたvector<vector<int>>をもとに、ボールの色を跳ね返したプレイやの色に変更する

		for (auto table : listCollide){
			cout << "hit by ballid=" << to_string(table.ballId) << endl;;
			b[table.ballId].setId(table.barId);
			b[table.ballId].setColor(idColor[table.barId]);
		}
	}
		vector<collisionList> checkCollideWithCircleField(){				//フィールド中心からr=300のフィールドを設定し、それと衝突したボールのidと中心からの角度を返す。
			vector<collisionList> collideList;							//返り値は{衝突したボールのid,X軸+側から時計回りにとった角度θ}						
			float fieldCtrX = DispFrameWidth / 2.0;
			float fieldCtrY = DispFrameHeight / 2.0;
			int ind = 0;												//配列変数bの添え字,範囲forであ添え字を取得できないので力技で
			for (auto bb : b){
				int r2_a = static_cast<int>(bb.getX() - fieldCtrX);
				int r2_b = static_cast<int>(bb.getY() - fieldCtrY);
				int r2_ball = pow(r2_a, 2) + pow(r2_b, 2);
				if (r2_ball > pow((DispFrameHeight / 2) - defaultBallR * 2, 2)){
					collisionList temp;
					temp.id = ind;
					temp.angle = atan2(r2_b, r2_a);
					collideList.push_back(temp);
					//cout << "r2_a=" << to_string(r2_a) << ":r2_b=" << to_string(r2_b) << ":r=" << to_string(r2_ball) + ":deg=" + to_string((atan2(r2_b , r2_a))/M_PI*180) << endl;
				}
				ind++;
			}
			return collideList;
		}


		void draw(Mat dest){
			//ボールを描画
			for (auto bb : b){
				bb.draw(dest);
			}
			//ゲームバーを描画
			for (auto pp : p){
				pp.draw(dest);
			}
		}
		void displayPlayerPoint(Mat& dest){		//得点を表示 
			switch (playerNum){
			case 2:
				putText(dest, to_string(p[1].getPoint()) + " - " + to_string(p[0].getPoint()), pointDisplayPos2Player, FONT_HERSHEY_COMPLEX, showPointScale, Scalar(255, 255, 255), 10);
				break;
			case 3:
				putText(dest, to_string(p[0].getPoint()), pointDisplayPos3Player[0], FONT_HERSHEY_COMPLEX, 3, Scalar(255, 255, 255), 2);
				putText(dest, to_string(p[1].getPoint()), pointDisplayPos3Player[1], FONT_HERSHEY_COMPLEX, 3, Scalar(255, 255, 255), 2);
				putText(dest, to_string(p[2].getPoint()), pointDisplayPos3Player[2], FONT_HERSHEY_COMPLEX, 3, Scalar(255, 255, 255), 2);
				break;
			default:
				break;
			}
		}

		void addBallScore(int id, int scr){
			p[id].addPoint(scr);
		}
		void setBallScore(int id, int scr){
			p[id].setPoint(scr);
		}
		void clearAllPoint(){
			for (int pId = 0; pId < playerNum;pId++){
				cout << "point=" << to_string(p[pId].getPoint()) << endl;
				p[pId].setPoint(0);
				cout << "point=" << to_string(p[pId].getPoint()) << endl;
				//cout << "point=" << to_string(pp.getPoint()) << endl;
			}
		}
		void resetAllBalls(){

		}
		bool isBallHitLeftWall(int ballid){
			return b[ballid].refL;
		}
		bool isBallHitRightWall(int ballid){
			return b[ballid].refR;
		}
		bool isBallHitUpWall(int ballid){		//上の壁と衝突しているか？
			return b[ballid].refU;
		}
		bool isBallHitDownWall(int ballid){
			return b[ballid].refD;
		}
		void resetBallPos(int ballid){
			b[ballid].setDafault();
		}
		int getPlayerbarPoint(int idp){
			return p[idp].getPoint();
		}
		void activeBall(int ballid){
			b[ballid].activate();
		}
		void deactiveBall(int ballid){
			b[ballid].deactivate();
		}
		void setBallInitVec(int ballid, Point2f vec){
			b[ballid].setAccelX(vec.x);
			b[ballid].setAccelY(vec.y);
		}
		int getPlayerNum(){
			return playerNum;
		}
		int getBallId(int id){ return b[id].getId(); }
		void setBallId(int idp, int s){ b[idp].setId(s); }
	};
	// 数値を２進数文字列に変換
	string to_binString(unsigned int val){
		if (!val)
			return std::string("0");
		std::string str;
		while (val != 0) {
			if ((val & 1) == 0)  // val は偶数か？
				str.insert(str.begin(), '0');  //  偶数の場合
			else
				str.insert(str.begin(), '1');  //  奇数の場合
			val >>= 1;
		}
		return str;
	};

	int main(int argc, char *argv[]){

		int key = 0;		//keyは押されたキー
		int loopTime = 0;
		int frame = 0;										//経過フレーム
		int winnerId = 0;									//勝利者のid
		int gamemode = 0;									//0:お絵かき　1:PONG
		bool isDebug = false;
		vector<ballBarCollisionList> PlayerBallCCollideListdebug;	//ボールとプレイヤーバーの衝突リスト

		vector<PointerData> PointData(LightMax);					//vectorオブジェクト使えや!!
		VideoCapture cap(0);
		cap.set(CV_CAP_PROP_FRAME_WIDTH, FrameWidth);
		cap.set(CV_CAP_PROP_FRAME_HEIGHT, FrameHeight);
		cout << "Initializing\n";

		Pong game(2, 1);

		if (!cap.isOpened()) {
			std::cout << "failed to capture camera";
			return -1;
		}
		namedWindow(windowNameDisp, CV_WINDOW_AUTOSIZE | CV_WINDOW_FREERATIO);
		namedWindow(windowNameThreshold2, CV_WINDOW_AUTOSIZE | CV_WINDOW_FREERATIO);
		namedWindow("blueImage", CV_WINDOW_AUTOSIZE | CV_WINDOW_FREERATIO);
		namedWindow("greenImage", CV_WINDOW_AUTOSIZE | CV_WINDOW_FREERATIO);
		namedWindow("redImage", CV_WINDOW_AUTOSIZE | CV_WINDOW_FREERATIO);

		while (1){

			double f = 1000.0 / cv::getTickFrequency();		//プログラム動作開始からのframe数を取得
			int64 time = cv::getTickCount();

			while (!cap.grab());
			cap.retrieve(rawCamera);

			cvtColor(rawCamera, Thresholded2, CV_BGR2GRAY);		//グレイスケール化
			threshold(Thresholded2, Thresholded2, GreyThreshold, 255, CV_THRESH_BINARY);

			//フレームの一部を捜査して、新しいLEDを検索する。ただし10刻み
			if (frame%findLightSpan){
				for (int y = 0; y < FrameHeight; y += lightSearchFrameSpan){
					const uchar *pLine = Thresholded2.ptr<uchar>(y);
					for (volatile int x = 0; x < FrameWidth; x += lightSearchFrameSpan){
						if (pLine[x] > GreyThreshold){
							bool isNew = true;					//true if LED is found newly
							for (int pointNum = 0; pointNum < LightMax - 0; pointNum++){	//発見した光点がすでに見つかっているPointdataと近ければ、すでに見つかっているものとする
								if ((abs(PointData[pointNum].getX() - x) < 100) & (abs(PointData[pointNum].getY() - y) < 100) & (PointData[pointNum].getAlive())){
									isNew = false;
									break;
								}
							}
							if (isNew){
								for (int PointNum = 0; PointNum < LightMax - 0; PointNum++){
									if (!PointData[PointNum].getAlive()){
										PointData[PointNum].newPoint(x, y);		//generator new LED
										cout << "newPoint found at (" << to_string(x) << "," << to_string(y) << ")" << endl;
										break;
									}
								}
							}
						}
					}
				}
			}

			//すべてのPointを重心を用いて位置情報の更新をする

			double gx, gy;
			Moments moment;

			for (int pointNum = 0; pointNum < LightMax - 0; pointNum++){		//check each LED on or off
				if (PointData[pointNum].getAlive()){							//指定したpointが生きているなら

					int cutposx = PointData[pointNum].getX() - LightMoveThreshold, cutposy = PointData[pointNum].getY() - LightMoveThreshold;	//cutposの指定座標が画面外になってエラーはくのを防ぐ
					if (cutposx < 0) cutposx = 0;
					if (cutposx > FrameWidth - 2 * LightMoveThreshold - 1) cutposx = FrameWidth - 2 * LightMoveThreshold - 1;
					if (cutposy < 0) cutposy = 0;
					if (cutposy > FrameHeight - 2 * LightMoveThreshold - 1) cutposy = FrameHeight - 2 * LightMoveThreshold - 1;
					Mat cut_img(Thresholded2, cvRect(cutposx, cutposy, 2 * LightMoveThreshold, 2 * LightMoveThreshold));				//LED光点周辺を切り取る

					moment = moments(cut_img, 1);																						//切り取ったcut_imgで新しいモーメントを計算
					gx = moment.m10 / moment.m00;																						//重心のX座標
					gy = moment.m01 / moment.m00;																						//重心のy座標

					if ((gx >= 0) && (gx <= 2 * LightMoveThreshold) && (gy >= 0) && (gy <= 2 * LightMoveThreshold)){					//gx,gyがcut_imgの範囲からはみ出すような異常な値か？

						int newX = PointData[pointNum].getX() + (int)(gx)-LightMoveThreshold;												//gx,gyにより更新されたXY座標が
						int newY = PointData[pointNum].getY() + (int)(gy)-LightMoveThreshold;
						bool isDub = false;																								//更新された座標はほかのpointとかぶっていないか？
						for (int pointNumToCheckDublication = 0; pointNumToCheckDublication < pointNum; pointNumToCheckDublication++){
							if ((abs(PointData[pointNumToCheckDublication].getX() - newX) < LightMoveThreshold) && (abs(PointData[pointNumToCheckDublication].getY() - newY) < LightMoveThreshold)){
								isDub = true;
								break;
							}
						}

						if (!isDub){									//ほかのPointDataと重複がないことが晴れて確認できたら
							PointData[pointNum].setXY(newX, newY);		//XY座標を更新	
						}
					}
					else{
					}

					//重心点の赤色成分がRedThreshold以上なら、赤LED点灯と見る
					if (rawCamera.at<Vec3b>(PointData[pointNum].getY() | 1, PointData[pointNum].getX())[2] > RedThreshold){

						if (PointData[pointNum].addToBin(1) == 1){
							cout << "point[" << to_string(pointNum) << "] is killed by TTD." << endl;
						}
					}
					else{
						if (PointData[pointNum].addToBin(0) == 1){
							cout << "point[" << to_string(pointNum) << "] is killed by TTD." << endl;
						}
					}

					//緑,青LEDからidを読み込む処理
					int id = -1;		

					if (rawCamera.at<Vec3b>(PointData[pointNum].getY() | 1, PointData[pointNum].getX())[0] > BlueThreshold){		//青色LEDは点灯しているか？
						if (rawCamera.at<Vec3b>(PointData[pointNum].getY() | 1, PointData[pointNum].getX())[1] > greenThreshold){		//緑色LEDは点灯しているか？
							//青と緑が点灯
							id = 2;
						}
						else{
							//青のみ点灯
							id = 1;
						}
					}
					else{
						if (rawCamera.at<Vec3b>(PointData[pointNum].getY() | 1, PointData[pointNum].getX())[1] > greenThreshold){		//緑色LEDは点灯しているか？
							//緑のみ点灯
							id = 0;
						}
					}
					PointData[pointNum].setId(id);			//読み取ったidをセット
				}
				else{

				}				
			}

			disp2 = disp.clone();	//disp2<-dispコピー dispは真っ黒なので実質的に黒塗りつぶし　

			//モードの処理gamemodeに応じてお絵かきとPONGを切り替える
			switch (gamemode){
			case 0:

				//お絵かき
				for (auto point : PointData){
					point.drawLine(disp);		//線を描く
				}
				break;

			case 1:

				//PlayerBarRenewInterbalフレームごとにプレイヤーバーを更新する
				if (frame%PlayerBarRenewInterbal == 0){
					game.updateBars(PointData, false);
				}

				game.activeBall(0);
				game.displayPlayerPoint(disp2);		//プレイヤーの得点を表示
				switch (game.getPlayerNum()){		//プレイヤー数で処理を分ける,２人のときは左右の壁の衝突判定をとり、3人は円形フィールドの判定をとる
					
					case 2:							//2人プレイ

						game.activeBall(0);
						game.moveBalls();
						if (game.isBallHitLeftWall(0)){		//左の壁にボールがぶつかったか
							game.addBallScore(0, 1);
							gamemode = 2;
							pongCnt = pointDispTime;
							game.resetBallPos(0);
							game.setBallInitVec(0, Point(-5, 0));
							if (game.getPlayerbarPoint(0) >= winConditionPoint){
								gamemode = 3;						//winConditionPointだけポイントをとったら勝利;
								winnerId = 0;					//勝者のプレイヤーid
							}
						}
						if (game.isBallHitRightWall(0)){		//右の壁にボールがぶつかったか
							game.addBallScore(1, 1);
							gamemode = 2;
							pongCnt = pointDispTime;
							game.resetBallPos(0);
							game.setBallInitVec(0, Point(5, 0));
							if (game.getPlayerbarPoint(1) >= winConditionPoint){
								gamemode = 3;						//winConditionPointだけポイントをとったら勝利;
								winnerId = 1;					//勝者のプレイヤーid
							}
						}
						break;

					case 3:							//3人プレイ
						game.activeBall(0);
						game.moveBalls();


						vector<collisionList> CircularFieldcollideList = game.checkCollideWithCircleField();		//ボールと円形フィールドの衝突を検出と接触したか調べ、リストを取得

						if (!CircularFieldcollideList.empty()){												//collideListから、円形フィールドに衝突したボールがあったとき
							for (auto con : CircularFieldcollideList){
								//if (con.size() == 0) break;

								int ballId = con.id;											//衝突したボールのid(直前にこのボールを跳ね返したプレイヤーのid)
								cout << "id=" << to_string(ballId);
								cout << "con.angle=" << to_string(con.angle);
								if ((con.angle > 0.0) && (con.angle < 2.0 / 3.0 * M_PI)){				//con.angleをもとに、角度ごとにどのプレイやの得点なのかを処理する（）
									if ((ballId == 0)) game.addBallScore(0, -1);						//自分のゴールに入れてしまったら-2点の減点
									//0度~2/3pi度ならid=0の得点
									game.addBallScore(0, -1);
									gamemode = 2;
									pongCnt = pointDispTime;
									game.resetBallPos(0);
									game.setBallInitVec(0, Point(0, 3));			//得点後のボールの初期ベクトル
									game.setBallId(0, 0);
									cout << "goal=0" << endl;
								}
								else if ((con.angle < 0.0) && (con.angle > -2.0 / 3.0 * M_PI)){
									//id=1の得点
									if (ballId == 1) game.addBallScore(1, -1);
									game.addBallScore(1, -1);
									gamemode = 2;
									pongCnt = pointDispTime;
									game.resetBallPos(0);
									game.setBallInitVec(0, Point(-3, 0));			//得点後のボールの初期ベクトル
									game.setBallId(0, 1);
									cout << "goal=1" << endl;
								}
								else{
									if (ballId == 2) game.addBallScore(2, -1);
									game.addBallScore(2, -1);
									gamemode = 2;
									pongCnt = pointDispTime;
									game.resetBallPos(0);
									game.setBallInitVec(0, Point(-5, -2));			//得点後のボールの初期ベクトル
									game.setBallId(0, 2);
									cout << "goal=2" << endl;
								}
								game.addBallScore(ballId, 1);						//最後にボールを入れたプレイヤを得点
								
								//フィールドとの衝突情報を元に、プレイやにスコアを追加する。
								if (game.getPlayerbarPoint(ballId) >= winConditionPoint){
								gamemode = 3;						//winConditionPointだけポイントをとったら勝利;
								winnerId = ballId;					//勝者のプレイヤーid
								}
								
							}

							break;
						}
					}
					break;

			case 2:							//pngCntが0になるまで点数を表示

				//PlayerBarRenewInterbalフレームごとにプレイヤーバーを更新する
				if (frame%PlayerBarRenewInterbal == 0){
					game.updateBars(PointData, false);
				}

				game.displayPlayerPoint(disp2);		//プレイヤーの得点を表示
				game.deactiveBall(0);
				pongCnt--;
				if (pongCnt == 0) gamemode = 1;
				break;

			case 3:							//規定点数を取ってゲーム終了

				//PlayerBarRenewInterbalフレームごとにプレイヤーバーを更新する

				game.displayPlayerPoint(disp2);		//プレイヤーの得点を表示
				game.deactiveBall(0);
				cv::putText(disp2, "Player " + to_string(winnerId) + " WIN", Point(130, 100), FONT_HERSHEY_COMPLEX, 2, Scalar(200, 200, 200), 5);
				break;
			}

			//colに応じた色の線をdispに描くお絵かき処理

			if (gamemode > 0){

				//プレイヤーバーとボールの衝突を判定し、衝突したボールのidと色を衝突したプレイヤーのidと色に変える
				if (frame > PlayerBarRenewInterbal*(lineNum + 1)){
					vector<ballBarCollisionList> PlayerBallCCollideList = game.checkPlayerBallCollide();				//プレイヤーバーとボールの衝突を調べ、リストを取得
					if (!PlayerBallCCollideList.empty()){
						game.changeAllBallColor(PlayerBallCCollideList);										//プレイヤーバーと衝突したボールの色を衝突リストをもとに変更する
						PlaySound("bounce.wav", NULL, SND_FILENAME | SND_ASYNC | SND_NOSTOP);					//反射音を再生
						PlayerBallCCollideListdebug = PlayerBallCCollideList;
					}
				}
			}

			for (int pointNum = 0; pointNum < LightMax - 0; pointNum++){		//display bin to 

				game.draw(disp2);		//PONGに関するものを表示

				//putText(disp2, "(x,y)=(" + to_string(PointData[pointNum].getX()) + "," + to_string(PointData[pointNum].getY()) + ")" + "id=" + to_string(PointData[pointNum].getId()) + ":color=" + to_string(PointData[pointNum].getColor()) + "pointData[" + to_string(pointNum) + "]", Point(DifDisplayX*PointData[pointNum].getX(), DifDisplayY*PointData[pointNum].getY()), FONT_HERSHEY_COMPLEX, 0.5, Scalar(200, 200, 200));
				putText(disp2, "id=" + to_string(PointData[pointNum].getId()) + ":color=" + to_string(PointData[pointNum].getColor()), Point(DifDisplayX*PointData[pointNum].getX(), DifDisplayY*PointData[pointNum].getY()), FONT_HERSHEY_COMPLEX, 0.5, Scalar(200, 200, 200));
				PointData[pointNum].drawCursor(disp2);
			}

			if (loopTime > 33){		//フレームレート表示
				putText(disp2, "fps=" + to_string(loopTime), Point(DispFrameWidth - 80, DispFrameHeight - 35), FONT_HERSHEY_COMPLEX, 0.5, Scalar(0, 0, 200));
			}
			else{
				putText(disp2, "fps=" + to_string(loopTime), Point(DispFrameWidth - 80, DispFrameHeight - 35), FONT_HERSHEY_COMPLEX, 0.5, Scalar(200, 200, 200));
			}

			putText(Thresholded2, "G_Thre=" + to_string(GreyThreshold) + ":R_Thre=" + to_string(RedThreshold) + ":B_Thre=" + to_string(BlueThreshold) + ":G_Thre=" + to_string(greenThreshold) + ":frame=" + to_string(frame) + ":gamemode=" + to_string(gamemode), Point(0, 10), FONT_HERSHEY_COMPLEX, 0.5, Scalar(200, 200, 200));

			if (isDebug){
				split(rawCamera, colorSplitDisp);
				putText(colorSplitDisp[0], ":BlueThreshold=" + to_string(BlueThreshold), Point(0, 10), FONT_HERSHEY_COMPLEX, 0.5, Scalar(200, 200, 200));
				putText(colorSplitDisp[1], ":GreenThreshold=" + to_string(greenThreshold), Point(0, 10), FONT_HERSHEY_COMPLEX, 0.5, Scalar(200, 200, 200));
				putText(colorSplitDisp[2], ":RedThreshold=" + to_string(RedThreshold), Point(0, 10), FONT_HERSHEY_COMPLEX, 0.5, Scalar(200, 200, 200));
				threshold(colorSplitDisp[0], colorSplitDisp[0], BlueThreshold, 255, CV_THRESH_BINARY);
				threshold(colorSplitDisp[1], colorSplitDisp[1], greenThreshold, 255, CV_THRESH_BINARY);
				threshold(colorSplitDisp[2], colorSplitDisp[2], RedThreshold, 255, CV_THRESH_BINARY);
				cv::imshow("blueImage", colorSplitDisp[0]);
				cv::imshow("greenImage", colorSplitDisp[1]);
				cv::imshow("redImage", colorSplitDisp[2]);
			}
			cv::imshow(windowNameDisp, disp2);
			cv::imshow(windowNameThreshold2, Thresholded2);

			key = waitKey(1);

			if (key == 'q'){					//終了
				destroyWindow(windowNameDisp);
				destroyWindow(windowNameThreshold2);
				return 0;
			}

			if (key == 'c'){
				disp = Scalar(0, 0, 0);			//dispを初期化
			}
			if (key == 'w'){					//greyThreshold +1
				if (GreyThreshold + 1 < 256) GreyThreshold++;
			}

			if (key == 's'){					//greyThreshold -1
				if (GreyThreshold - 1 >= 0) GreyThreshold--;
			}

			if (key == 'e'){					//RedThreshold +1
				if (RedThreshold + 1 < 256) RedThreshold++;
			}

			if (key == 'd'){					//RedThreshold -1
				if (RedThreshold - 1 >= 0) RedThreshold--;
			}
			if (key == 'r'){					//BlueThreshold +1
				if (BlueThreshold + 1 < 256) BlueThreshold++;
			}

			if (key == 'f'){					//blueThreshold -1
				if (BlueThreshold - 1 >= 0) BlueThreshold--;
			}
			if (key == 't'){					//blueThreshold -1
				if (greenThreshold + 1 < 256) greenThreshold++;
			}
			if (key == 'g'){					//blueThreshold -1
				if (greenThreshold - 1 >= 0) greenThreshold--;
			}
			if (key == 'h') {					//デバッグモード
				isDebug = !isDebug;
			}
			if (key == 'u'){					//PONGを開始		
				game.startGame();
				gamemode = 1;
			}
			if (key == 'j'){					//PONGを終了
				game.endGame();
				gamemode = 0;
			}

			loopTime = (cv::getTickCount() - time)*f;
			frame++;
		}
	}

