package android.os;
public final class CancellationSignal {
    private volatile Runnable listener;
    private volatile boolean cancelled;
    public void setOnCancelListener(Runnable value){listener=value;if(cancelled)value.run();}
    public void cancel(){cancelled=true;Runnable value=listener;if(value!=null)value.run();}
}
