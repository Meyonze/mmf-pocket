package android.view;
import android.content.Context;
import android.graphics.Canvas;
public class View {
    public static final int VISIBLE=0,IMPORTANT_FOR_ACCESSIBILITY_NO=2;
    public View(Context context){}
    public void setImportantForAccessibility(int value){}
    public void invalidate(){}
    protected void onDraw(Canvas canvas){}
    public int getWidth(){return 84;}
    public int getHeight(){return 92;}
    public boolean isShown(){return true;}
    public int getWindowVisibility(){return VISIBLE;}
    public void postInvalidateOnAnimation(){}
    public void postInvalidateDelayed(long delay){}
}
