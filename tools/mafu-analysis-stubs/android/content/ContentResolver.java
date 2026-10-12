package android.content;
import android.content.res.AssetFileDescriptor;
import android.net.Uri;
import android.os.CancellationSignal;
import java.io.IOException;
public abstract class ContentResolver {
    public abstract AssetFileDescriptor openAssetFileDescriptor(Uri uri,String mode,CancellationSignal signal)throws IOException;
}
