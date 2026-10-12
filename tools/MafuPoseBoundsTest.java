package app.mmfpocket.player;

import java.lang.reflect.Field;
import java.lang.reflect.Method;

/** Actual view's pose math + actual opaque mesh. Canvas/device drawing is not exercised. */
public final class MafuPoseBoundsTest {
    static float value(Object object,String field)throws Exception {
        Field f=object.getClass().getDeclaredField(field);f.setAccessible(true);return f.getFloat(object);
    }
    public static void main(String[] args)throws Exception {
        IllustratedMafuMascotView view=new IllustratedMafuMascotView(null);
        Field f=IllustratedMafuMascotView.class.getDeclaredField("audioDance");f.setAccessible(true);
        MafuAudioDance dance=(MafuAudioDance)f.get(view);
        Method pose=IllustratedMafuMascotView.class.getDeclaredMethod("calculateAudioMotion",float.class);pose.setAccessible(true);
        MafuMesh mesh=new MafuMesh(192);double minX=999,minY=999,maxX=-999,maxY=-999;int poses=0;
        for(int action=-1;action<4;action++)for(int step=0;step<=24;step++) {
            dance.action=action;dance.actionSeconds=action<0?0:step/24f*MafuAudioDance.duration(action);
            dance.intensity=1;dance.level=.95f;dance.bass=.8f;dance.treble=.8f;dance.pulse=.95f;dance.lean=step%2==0?.8f:-.8f;dance.phase=step*.137f;
            Object motion=pose.invoke(view,3f+step*.03f);
            mesh.render(value(motion,"flipXDegrees"),value(motion,"turnYDegrees"));
            double roll=Math.toRadians(value(motion,"rotation")),cos=Math.cos(roll),sin=Math.sin(roll);
            for(int i=0;i<mesh.pixels.length;i++) {
                if(mesh.pixels[i]==0)continue;
                double x=((i%192+.5)*64/192-32)*value(motion,"scaleX");
                double y=((i/192+.5)*64/192-32)*value(motion,"scaleY");
                double screenX=42+(value(motion,"x")+cos*x-sin*y)*84/80;
                double screenY=46+(value(motion,"y")+sin*x+cos*y)*84/80;
                minX=Math.min(minX,screenX);maxX=Math.max(maxX,screenX);
                minY=Math.min(minY,screenY);maxY=Math.max(maxY,screenY);
            }
            ++poses;
        }
        System.out.printf(java.util.Locale.ROOT,"Actual opaque mesh bounds over%d strong-beat/action poses in84x92 card: x=%.3f..%.3f y=%.3f..%.3f%n",poses,minX,maxX,minY,maxY);
        if(minX<1||minY<1||maxX>83||maxY>91)throw new AssertionError("enlarged motion must retain card headroom");
        System.out.println("PASS actual pose/mesh headroom. No Android Canvas/GPU/frame-rate/device test.");
    }
}
