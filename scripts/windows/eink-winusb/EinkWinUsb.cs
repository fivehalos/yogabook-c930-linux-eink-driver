/*
 * Minimal WinUSB client for YogaBook C930 ITE8951 (048d:8951 MI_00).
 * Protocol framing mirrors kernel/eink_drm/protocol/ite8951_usb.c
 *
 * Build (Developer PowerShell or any shell with .NET Framework csc):
 *   powershell -ExecutionPolicy Bypass -File .\scripts\windows\eink-winusb\build.ps1
 *
 * Run elevated only if needed; WinUSB usually works as normal user when EinkSvr is stopped:
 *   .\scripts\windows\eink-winusb\EinkWinUsb.exe scenario-get
 *   .\scripts\windows\eink-winusb\EinkWinUsb.exe scenario-set 3
 *   .\scripts\windows\eink-winusb\EinkWinUsb.exe pen-mouse
 */
using System;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
using System.Collections.Generic;
using System.IO;

internal static class Native
{
	public static readonly Guid IteWinUsbGuid =
		new Guid("F0CFF988-E528-4B4A-8CE8-2F70DA273649");

	public const int DIGCF_PRESENT = 0x2;
	public const int DIGCF_DEVICEINTERFACE = 0x10;
	public const uint GENERIC_READ = 0x80000000;
	public const uint GENERIC_WRITE = 0x40000000;
	public const uint FILE_SHARE_READ = 0x1;
	public const uint FILE_SHARE_WRITE = 0x2;
	public const uint OPEN_EXISTING = 3;
	public const uint FILE_ATTRIBUTE_NORMAL = 0x80;
	public const uint FILE_FLAG_OVERLAPPED = 0x40000000;

	[StructLayout(LayoutKind.Sequential)]
	public struct SP_DEVICE_INTERFACE_DATA
	{
		public int cbSize;
		public Guid InterfaceClassGuid;
		public int Flags;
		public IntPtr Reserved;
	}

	[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
	public struct SP_DEVICE_INTERFACE_DETAIL_DATA
	{
		public int cbSize;
		[MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
		public string DevicePath;
	}

	[StructLayout(LayoutKind.Sequential)]
	public struct USB_INTERFACE_DESCRIPTOR
	{
		public byte bLength;
		public byte bDescriptorType;
		public byte bInterfaceNumber;
		public byte bAlternateSetting;
		public byte bNumEndpoints;
		public byte bInterfaceClass;
		public byte bInterfaceSubClass;
		public byte bInterfaceProtocol;
		public byte iInterface;
	}

	/* Matches winusb.h layout (default alignment; do not Pack=1). */
	[StructLayout(LayoutKind.Sequential)]
	public struct WINUSB_PIPE_INFORMATION
	{
		public int PipeType; /* USBD_PIPE_TYPE enum */
		public byte PipeId;
		public ushort MaximumPacketSize;
		public byte Interval;
	}

	[DllImport("setupapi.dll", CharSet = CharSet.Auto, SetLastError = true)]
	public static extern IntPtr SetupDiGetClassDevs(ref Guid classGuid, IntPtr enumerator, IntPtr hwndParent, int flags);

	[DllImport("setupapi.dll", SetLastError = true)]
	public static extern bool SetupDiEnumDeviceInterfaces(IntPtr deviceInfoSet, IntPtr deviceInfoData, ref Guid interfaceClassGuid, int memberIndex, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData);

	[DllImport("setupapi.dll", CharSet = CharSet.Auto, SetLastError = true)]
	public static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr deviceInfoSet, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData, ref SP_DEVICE_INTERFACE_DETAIL_DATA deviceInterfaceDetailData, int deviceInterfaceDetailDataSize, out int requiredSize, IntPtr deviceInfoData);

	[DllImport("setupapi.dll", CharSet = CharSet.Auto, SetLastError = true)]
	public static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr deviceInfoSet, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData, IntPtr deviceInterfaceDetailData, int deviceInterfaceDetailDataSize, out int requiredSize, IntPtr deviceInfoData);

	[DllImport("setupapi.dll", SetLastError = true)]
	public static extern bool SetupDiDestroyDeviceInfoList(IntPtr deviceInfoSet);

	[DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
	public static extern SafeFileHandle CreateFile(string lpFileName, uint dwDesiredAccess, uint dwShareMode, IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);

	[DllImport("winusb.dll", SetLastError = true)]
	public static extern bool WinUsb_Initialize(SafeFileHandle DeviceHandle, out IntPtr InterfaceHandle);

	[DllImport("winusb.dll", SetLastError = true)]
	public static extern bool WinUsb_Free(IntPtr InterfaceHandle);

	[DllImport("winusb.dll", SetLastError = true)]
	public static extern bool WinUsb_QueryInterfaceSettings(IntPtr InterfaceHandle, byte AlternateSettingNumber, out USB_INTERFACE_DESCRIPTOR UsbAltInterfaceDescriptor);

	[DllImport("winusb.dll", SetLastError = true)]
	public static extern bool WinUsb_QueryPipe(IntPtr InterfaceHandle, byte AlternateInterfaceNumber, byte PipeIndex, out WINUSB_PIPE_INFORMATION PipeInformation);

	[DllImport("winusb.dll", SetLastError = true)]
	public static extern bool WinUsb_WritePipe(IntPtr InterfaceHandle, byte PipeID, byte[] Buffer, int BufferLength, out int LengthTransferred, IntPtr Overlapped);

	[DllImport("winusb.dll", SetLastError = true)]
	public static extern bool WinUsb_ReadPipe(IntPtr InterfaceHandle, byte PipeID, byte[] Buffer, int BufferLength, out int LengthTransferred, IntPtr Overlapped);

	[DllImport("winusb.dll", SetLastError = true)]
	public static extern bool WinUsb_FlushPipe(IntPtr InterfaceHandle, byte PipeID);

	[DllImport("winusb.dll", SetLastError = true)]
	public static extern bool WinUsb_AbortPipe(IntPtr InterfaceHandle, byte PipeID);

	[DllImport("winusb.dll", SetLastError = true)]
	public static extern bool WinUsb_ResetPipe(IntPtr InterfaceHandle, byte PipeID);

	[DllImport("winusb.dll", SetLastError = true)]
	public static extern bool WinUsb_SetPipePolicy(IntPtr InterfaceHandle, byte PipeID, uint PolicyType, int ValueLength, ref int Value);

	[DllImport("winusb.dll", SetLastError = true)]
	public static extern bool WinUsb_GetOverlappedResult(IntPtr InterfaceHandle, IntPtr lpOverlapped, out int lpNumberOfBytesTransferred, bool bWait);

	[DllImport("kernel32.dll", SetLastError = true)]
	public static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

	[StructLayout(LayoutKind.Sequential)]
	public struct OVERLAPPED
	{
		public IntPtr Internal;
		public IntPtr InternalHigh;
		public int Offset;
		public int OffsetHigh;
		public IntPtr hEvent;
	}

	public const uint PIPE_TRANSFER_TIMEOUT = 0x03;
	public const uint WAIT_OBJECT_0 = 0;
	public const uint WAIT_TIMEOUT = 0x102;
	public const uint INFINITE = 0xFFFFFFFF;
}

internal sealed class IteDevice : IDisposable
{
	public const byte OpGetSys = 0x80;
	public const byte OpReadMem = 0x81; /* E MT entry short read */
	public const byte OpReadReg = 0x83;
	public const byte OpWriteReg = 0x84;
	public const byte OpDpyArea = 0x94;
	public const byte OpScenario = 0xA6;
	public const byte OpLdImg = 0xA8;
	public const byte OpSetWaveform = 0xA9;
	public const byte OpSetHandwr = 0xAC;
	public const byte OpAe = 0xAE; /* E MT entry; meaning open */
	public const byte OpSetTpArea = 0xAF;
	public const byte OpGetDpyStatus = 0xB1;
	public const byte OpDynamic = 0xB3;

	public const byte ScenarioNormal = 0;
	public const byte ScenarioKeyboard = 1;
	public const byte ScenarioPenMouse = 3;

	/* Linux-validated xfer/blit base (do not use GET_SYS image_buf). */
	public const uint ImageBufBase = 0x00382f30u;
	public const uint PanelWidth = 1920;
	public const uint PanelHeight = 1080;
	public const uint MaxXferBytes = 61440; /* 1920×32 */
	public const uint WaveformCurrent = 0xff;
	public const int DpyStatusBytes = 136;

	static readonly byte[] UsbcHeader = { 0x55, 0x53, 0x42, 0x43, 0x61, 0x89, 0x51, 0x89 };

	readonly SafeFileHandle _file;
	readonly IntPtr _winusb;
	readonly byte _pipeOut;
	readonly byte _pipeIn;

	IteDevice(SafeFileHandle file, IntPtr winusb, byte pipeOut, byte pipeIn)
	{
		_file = file;
		_winusb = winusb;
		_pipeOut = pipeOut;
		_pipeIn = pipeIn;
	}

	public static IteDevice Open()
	{
		Guid guid = Native.IteWinUsbGuid;
		IntPtr info = Native.SetupDiGetClassDevs(ref guid, IntPtr.Zero, IntPtr.Zero,
			Native.DIGCF_PRESENT | Native.DIGCF_DEVICEINTERFACE);
		if (info == new IntPtr(-1))
			throw new InvalidOperationException("SetupDiGetClassDevs failed: " + Marshal.GetLastWin32Error());

		try
		{
			for (int i = 0; ; i++)
			{
				var ifData = new Native.SP_DEVICE_INTERFACE_DATA();
				ifData.cbSize = Marshal.SizeOf(ifData);
				if (!Native.SetupDiEnumDeviceInterfaces(info, IntPtr.Zero, ref guid, i, ref ifData))
					break;

				int required;
				Native.SetupDiGetDeviceInterfaceDetail(info, ref ifData, IntPtr.Zero, 0, out required, IntPtr.Zero);
				IntPtr detailBuf = Marshal.AllocHGlobal(required);
				try
				{
					Marshal.WriteInt32(detailBuf, IntPtr.Size == 8 ? 8 : 6);
					if (!Native.SetupDiGetDeviceInterfaceDetail(info, ref ifData, detailBuf, required, out required, IntPtr.Zero))
						throw new InvalidOperationException("SetupDiGetDeviceInterfaceDetail failed: " + Marshal.GetLastWin32Error());

					string path = Marshal.PtrToStringAuto(IntPtr.Add(detailBuf, 4));
					Console.WriteLine("Opening " + path);

					SafeFileHandle file = Native.CreateFile(path,
						Native.GENERIC_READ | Native.GENERIC_WRITE,
						Native.FILE_SHARE_READ | Native.FILE_SHARE_WRITE,
						IntPtr.Zero, Native.OPEN_EXISTING,
						Native.FILE_ATTRIBUTE_NORMAL | Native.FILE_FLAG_OVERLAPPED,
						IntPtr.Zero);
					if (file.IsInvalid)
						throw new InvalidOperationException("CreateFile failed: " + Marshal.GetLastWin32Error() +
							" (is EinkSvr stopped? another handle open?)");

					IntPtr winusb;
					if (!Native.WinUsb_Initialize(file, out winusb))
					{
						int err = Marshal.GetLastWin32Error();
						file.Dispose();
						throw new InvalidOperationException("WinUsb_Initialize failed: " + err);
					}

					/* Captures + ITE interface: bulk OUT 0x02, bulk IN 0x81. */
					byte pipeOut = 0x02;
					byte pipeIn = 0x81;
					Native.USB_INTERFACE_DESCRIPTOR desc;
					if (Native.WinUsb_QueryInterfaceSettings(winusb, 0, out desc))
					{
						Console.WriteLine("Interface eps={0} class=0x{1:X2}", desc.bNumEndpoints, desc.bInterfaceClass);
						for (byte p = 0; p < desc.bNumEndpoints; p++)
						{
							Native.WINUSB_PIPE_INFORMATION pipe;
							if (!Native.WinUsb_QueryPipe(winusb, 0, p, out pipe))
								continue;
							Console.WriteLine("  pipe[{0}] type={1} id=0x{2:X2} mps={3}",
								p, pipe.PipeType, pipe.PipeId, pipe.MaximumPacketSize);
							if (pipe.PipeType == 2 || pipe.PipeType == 3)
							{
								if ((pipe.PipeId & 0x80) != 0)
									pipeIn = pipe.PipeId;
								else
									pipeOut = pipe.PipeId;
							}
						}
					}

					int timeout = 10000;
					Native.WinUsb_SetPipePolicy(winusb, pipeOut, Native.PIPE_TRANSFER_TIMEOUT, 4, ref timeout);
					Native.WinUsb_SetPipePolicy(winusb, pipeIn, Native.PIPE_TRANSFER_TIMEOUT, 4, ref timeout);

					/* Clear a stranded CSW from a previous process that forgot to drain. */
					int drainTimeout = 200;
					Native.WinUsb_SetPipePolicy(winusb, pipeIn, Native.PIPE_TRANSFER_TIMEOUT, 4, ref drainTimeout);
					byte[] junk = new byte[64];
					int junkXfer;
					while (Native.WinUsb_ReadPipe(winusb, pipeIn, junk, junk.Length, out junkXfer, IntPtr.Zero))
					{
						if (junkXfer <= 0)
							break;
					}
					Native.WinUsb_SetPipePolicy(winusb, pipeIn, Native.PIPE_TRANSFER_TIMEOUT, 4, ref timeout);

					Console.WriteLine("Bulk OUT=0x{0:X2} IN=0x{1:X2}", pipeOut, pipeIn);
					return new IteDevice(file, winusb, pipeOut, pipeIn);
				}
				finally
				{
					Marshal.FreeHGlobal(detailBuf);
				}
			}
		}
		finally
		{
			Native.SetupDiDestroyDeviceInfoList(info);
		}

		throw new InvalidOperationException("No ITE WinUSB interface found (GUID F0CFF988-...). Is MI_00 present?");
	}

	public void Dispose()
	{
		if (_winusb != IntPtr.Zero)
			Native.WinUsb_Free(_winusb);
		if (_file != null && !_file.IsClosed)
			_file.Dispose();
	}

	static byte[] BuildCtrl(bool expectResponse, uint responseLength, byte iteOpcode,
		uint address, ushort arg1, ushort arg2, ushort arg3, ushort arg4)
	{
		byte[] pkt = new byte[31];
		Buffer.BlockCopy(UsbcHeader, 0, pkt, 0, 8);
		BitConverter.GetBytes(responseLength).CopyTo(pkt, 8); // LE
		pkt[12] = expectResponse ? (byte)0x80 : (byte)0x00;
		pkt[13] = 0;
		pkt[14] = 0x10;
		pkt[15] = 0xfe;
		pkt[16] = 0; // lun
		WriteBe32(pkt, 17, address);
		pkt[21] = iteOpcode;
		WriteBe16(pkt, 22, arg1);
		WriteBe16(pkt, 24, arg2);
		WriteBe16(pkt, 26, arg3);
		WriteBe16(pkt, 28, arg4);
		pkt[30] = 0;
		return pkt;
	}

	static void WriteBe32(byte[] b, int off, uint v)
	{
		b[off] = (byte)(v >> 24);
		b[off + 1] = (byte)(v >> 16);
		b[off + 2] = (byte)(v >> 8);
		b[off + 3] = (byte)v;
	}

	static void WriteBe16(byte[] b, int off, ushort v)
	{
		b[off] = (byte)(v >> 8);
		b[off + 1] = (byte)v;
	}

	/*
	 * Overlapped WinUsb I/O. Only Wait when the call returns ERROR_IO_PENDING;
	 * a successful sync completion must not Wait (stale GetLastError=997 caused
	 * false waits / timeouts that wedged the pipe).
	 */
	bool PipeIo(bool write, byte[] buffer, int length, out int transferred, string step)
	{
		transferred = 0;
		IntPtr ovMem = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(Native.OVERLAPPED)));
		IntPtr evt = IntPtr.Zero;
		byte pipe = write ? _pipeOut : _pipeIn;
		try
		{
			evt = CreateEvent(IntPtr.Zero, true, false, null);
			if (evt == IntPtr.Zero)
				throw new InvalidOperationException(step + " CreateEvent failed");

			var ov = new Native.OVERLAPPED();
			ov.hEvent = evt;
			Marshal.StructureToPtr(ov, ovMem, false);

			bool ok = write
				? Native.WinUsb_WritePipe(_winusb, _pipeOut, buffer, length, out transferred, ovMem)
				: Native.WinUsb_ReadPipe(_winusb, _pipeIn, buffer, length, out transferred, ovMem);
			int err = Marshal.GetLastWin32Error();

			if (ok)
				return true;

			if (err != 997) /* ERROR_IO_PENDING */
			{
				Native.WinUsb_AbortPipe(_winusb, pipe);
				throw new InvalidOperationException(step + " failed immediately: " + err);
			}

			uint wr = Native.WaitForSingleObject(evt, 10000);
			if (wr != Native.WAIT_OBJECT_0)
			{
				Native.WinUsb_AbortPipe(_winusb, pipe);
				Native.WinUsb_ResetPipe(_winusb, pipe);
				throw new InvalidOperationException(step + " wait failed: " + wr);
			}

			if (!Native.WinUsb_GetOverlappedResult(_winusb, ovMem, out transferred, false))
			{
				err = Marshal.GetLastWin32Error();
				Native.WinUsb_AbortPipe(_winusb, pipe);
				throw new InvalidOperationException(step + " GetOverlappedResult: " + err);
			}
			return true;
		}
		finally
		{
			if (evt != IntPtr.Zero)
				CloseHandle(evt);
			Marshal.FreeHGlobal(ovMem);
		}
	}

	[DllImport("kernel32.dll", SetLastError = true)]
	static extern IntPtr CreateEvent(IntPtr lpEventAttributes, bool bManualReset, bool bInitialState, string lpName);

	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool CloseHandle(IntPtr hObject);

	void BulkOut(byte[] data, string step)
	{
		int xfer;
		PipeIo(true, data, data.Length, out xfer, step + "-out");
		if (xfer != data.Length)
			throw new InvalidOperationException(step + " short write " + xfer);
	}

	byte[] BulkIn(int maxLen, string step)
	{
		byte[] buf = new byte[maxLen];
		int xfer;
		PipeIo(false, buf, buf.Length, out xfer, step + "-in");
		byte[] got = new byte[xfer];
		Buffer.BlockCopy(buf, 0, got, 0, xfer);
		return got;
	}

	static readonly byte[] UsbsMagic = { 0x55, 0x53, 0x42, 0x53 }; /* "USBS" */
	const int StatusBytes = 13;

	static bool LooksLikeStatus(byte[] data, int offset)
	{
		if (data == null || data.Length < offset + 4)
			return false;
		for (int i = 0; i < 4; i++)
			if (data[offset + i] != UsbsMagic[i])
				return false;
		return true;
	}

	/* BOT: device will not accept the next CBW until CSW (USBS) is drained. */
	void DrainStatus(string step, byte[] already)
	{
		if (already != null)
		{
			for (int i = 0; i <= already.Length - 4; i++)
			{
				if (LooksLikeStatus(already, i))
					return;
			}
		}

		try
		{
			byte[] status = BulkIn(64, step + "-status");
			if (LooksLikeStatus(status, 0))
				return;
			/* First IN may be a leftover data short-packet; take CSW next. */
			BulkIn(64, step + "-status2");
		}
		catch (Exception ex)
		{
			Console.WriteLine("  warn: status drain: " + ex.Message);
		}
	}

	public byte[] Exchange(byte[] ctrl, int responseLen, string step)
	{
		BulkOut(ctrl, step + "-out");
		if (responseLen <= 0)
		{
			byte[] status = BulkIn(64, step + "-status");
			return status;
		}

		/* Short-packet IN often returns only the response; CSW is a second IN.
		 * Skipping it wedges the next OUT (wait 258) across process runs. */
		byte[] resp = BulkIn(Math.Max(responseLen + StatusBytes, 64), step + "-in");
		byte[] sliced = resp;
		if (resp.Length > responseLen)
		{
			sliced = new byte[responseLen];
			Buffer.BlockCopy(resp, 0, sliced, 0, responseLen);
		}
		DrainStatus(step, resp);
		return sliced;
	}

	public byte[] ExchangeOutPayload(byte[] ctrl, byte[] payload, string step)
	{
		BulkOut(ctrl, step + "-ctrl");
		BulkOut(payload, step + "-payload");
		byte[] status = BulkIn(64, step + "-status");
		DrainStatus(step, status);
		return status;
	}

	public void WaitDisplayReady(int timeoutMs)
	{
		DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
		for (;;)
		{
			byte[] ctrl = BuildCtrl(true, DpyStatusBytes, OpGetDpyStatus, 0, 0, 0, 0, 0);
			byte[] resp = Exchange(ctrl, DpyStatusBytes, "DPY_STATUS");
			if (resp.Length < 6)
				throw new InvalidOperationException("short DPY_STATUS");
			ushort busy = BitConverter.ToUInt16(resp, 4); /* LE engine_busy */
			if (busy == 0)
				return;
			if (DateTime.UtcNow > deadline)
				throw new TimeoutException("display engine busy timeout");
			System.Threading.Thread.Sleep(50);
		}
	}

	/*
	 * LD_IMG: packed w*h bytes at base+bufOffset (Linux path).
	 * Full-frame stripes use bufOffset = y * PanelWidth.
	 */
	public void LoadPixels(uint bufOffset, uint w, uint h, byte[] pixels)
	{
		if (w == 0 || h == 0)
			throw new ArgumentException("empty region");
		long expected = (long)w * h;
		if (pixels == null || pixels.Length < expected)
			throw new ArgumentException("pixel buffer short");
		if (expected > MaxXferBytes)
			throw new ArgumentException("region exceeds 61440 byte LD_IMG limit");

		uint loadAddr = ImageBufBase + bufOffset;
		byte[] slice = pixels;
		if (pixels.Length != expected)
		{
			slice = new byte[expected];
			Buffer.BlockCopy(pixels, 0, slice, 0, (int)expected);
		}
		byte[] ctrl = BuildCtrl(false, (uint)expected, OpLdImg, loadAddr, 0, 0,
			(ushort)w, (ushort)h);
		ExchangeOutPayload(ctrl, slice, "LD_IMG");
	}

	/* Blit mem_addr = base + bufOffset - x - 1920*y (BLUEPRINT). */
	public void Blit(uint bufOffset, uint x, uint y, uint w, uint h, uint waveform)
	{
		WaitDisplayReady(10000);
		uint memAddr = ImageBufBase + bufOffset - x - PanelWidth * y;
		byte[] payload = new byte[24];
		WriteBe32(payload, 0, memAddr);
		WriteBe32(payload, 4, waveform);
		WriteBe32(payload, 8, x);
		WriteBe32(payload, 12, y);
		WriteBe32(payload, 16, w);
		WriteBe32(payload, 20, h);
		byte[] ctrl = BuildCtrl(false, (uint)payload.Length, OpDpyArea, 0, 0, 0, 0, 0);
		Console.WriteLine("  blit mem=0x{0:X8} wf=0x{1:X} ({2},{3}) {4}x{5}",
			memAddr, waveform, x, y, w, h);
		ExchangeOutPayload(ctrl, payload, "BLIT");
		WaitDisplayReady(10000);
	}

	public void FillRect(uint x, uint y, uint w, uint h, byte gray)
	{
		const uint stripeH = 32;
		uint endY = y + h;
		/* FB-layout store: packed full-width rows live at base + y*stride (+x). */
		for (uint row = y; row < endY; )
		{
			uint th = Math.Min(stripeH, endY - row);
			uint bytes = w * th;
			byte[] pix = new byte[bytes];
			for (int i = 0; i < pix.Length; i++)
				pix[i] = gray;
			uint bufOff = row * PanelWidth + x;
			LoadPixels(bufOff, w, th, pix);
			row += th;
		}
		/* bufOffset matches FB origin used above (content for dest starts at base+0). */
		Blit(0, x, y, w, h, WaveformCurrent);
	}

	public void FillScreen(byte gray)
	{
		Console.WriteLine("Fill screen gray=0x{0:X2} ({1}x{2})...", gray, PanelWidth, PanelHeight);
		const uint stripeH = 32;
		for (uint row = 0; row < PanelHeight; )
		{
			uint th = Math.Min(stripeH, PanelHeight - row);
			byte[] pix = new byte[PanelWidth * th];
			for (int i = 0; i < pix.Length; i++)
				pix[i] = gray;
			LoadPixels(row * PanelWidth, PanelWidth, th, pix);
			row += th;
		}
		Blit(0, 0, 0, PanelWidth, PanelHeight, WaveformCurrent);
		Console.WriteLine("Fill done.");
	}

	/* Alternating 64px bands — easy to see what a blit clears vs ghost. */
	public void FillStripes()
	{
		Console.WriteLine("Fill horizontal stripes (64px black/white)...");
		const uint band = 64;
		const uint stripeH = 32;
		for (uint y = 0; y < PanelHeight; y += band)
		{
			uint h = Math.Min(band, PanelHeight - y);
			byte gray = ((y / band) % 2 == 0) ? (byte)0x00 : (byte)0xff;
			for (uint row = y; row < y + h; )
			{
				uint th = Math.Min(stripeH, y + h - row);
				byte[] pix = new byte[PanelWidth * th];
				for (int i = 0; i < pix.Length; i++)
					pix[i] = gray;
				LoadPixels(row * PanelWidth, PanelWidth, th, pix);
				row += th;
			}
		}
		Blit(0, 0, 0, PanelWidth, PanelHeight, WaveformCurrent);
		Console.WriteLine("Stripes done.");
	}

	/*
	 * Replay early E-multitouch mode-entry (Homebar path) without EinkSvr.
	 * A6 uses addr 0x01030100 as seen in E-multitouch-penmouse.pcap.
	 */
	public void MtModeReplay()
	{
		Console.WriteLine("Before MT replay: scenario GET...");
		byte before = ScenarioGet();
		Console.WriteLine("scenario={0}", before);

		Console.WriteLine("B3 0101/0003/0301 + A9 + A6 0x01030100...");
		try { DynamicCdb(0x0101, 0x0003, 0x0301, 0); }
		catch (Exception ex) { Console.WriteLine("  warn: " + ex.Message); }
		try { SetWaveform(0x200); } catch (Exception ex) { Console.WriteLine("  warn: " + ex.Message); }
		{
			byte[] ctrl = BuildCtrl(true, 4, OpScenario, 0x01030100u, 0, 0, 0, 0);
			byte[] resp = Exchange(ctrl, 4, "SCENARIO_MT");
			Console.WriteLine("  A6 0x01030100 rsp: {0}",
				BitConverter.ToString(resp, 0, Math.Min(resp.Length, 8)));
		}

		Console.WriteLine("B3 0100/0003/0301 + A9 + A6 0x01030100...");
		try { DynamicCdb(0x0100, 0x0003, 0x0301, 0); }
		catch (Exception ex) { Console.WriteLine("  warn: " + ex.Message); }
		try { SetWaveform(0x200); } catch (Exception ex) { Console.WriteLine("  warn: " + ex.Message); }
		{
			byte[] ctrl = BuildCtrl(true, 4, OpScenario, 0x01030100u, 0, 0, 0, 0);
			byte[] resp = Exchange(ctrl, 4, "SCENARIO_MT2");
			Console.WriteLine("  A6 0x01030100 rsp: {0}",
				BitConverter.ToString(resp, 0, Math.Min(resp.Length, 8)));
		}

		byte after = ScenarioGet();
		Console.WriteLine("After MT replay: scenario={0} (want != 1)", after);
	}

	public byte[] GetSys()
	{
		byte[] ctrl = BuildCtrl(true, 112, OpGetSys, 0, 0, 0, 0, 0);
		return Exchange(ctrl, 112, "GET_SYS");
	}

	/*
	 * C-penmouse.pcap: GET addr=0 returns 4 bytes; keyboard is byte1==1,
	 * owner-draw / pen path is byte1==0. Never saw address==3 on the wire.
	 * Keyboard SET uses address = scenario<<24 (0x01000000), response echoes it.
	 */
	public byte ScenarioGet()
	{
		byte[] ctrl = BuildCtrl(true, 4, OpScenario, 0, 0, 0, 0, 0);
		byte[] resp = Exchange(ctrl, 4, "SCENARIO_GET");
		Console.WriteLine("  raw GET rsp: {0}", BitConverter.ToString(resp, 0, Math.Min(resp.Length, 8)));
		if (resp.Length < 2)
			throw new InvalidOperationException("short scenario response");
		return resp[1];
	}

	/* Windows EiSetScenario: scenario in address high byte (BE). */
	public void ScenarioSetHiByte(byte scenario)
	{
		uint addr = ((uint)scenario) << 24;
		byte[] ctrl = BuildCtrl(true, 4, OpScenario, addr, 0, 0, 0, 0);
		byte[] resp = Exchange(ctrl, 4, "SCENARIO_SET_HIBYTE");
		Console.WriteLine("  SET addr=0x{0:X8} rsp: {1}", addr,
			BitConverter.ToString(resp, 0, Math.Min(resp.Length, 8)));
	}

	/* Older guess: scenario as low address DWORD (not seen in C/S captures). */
	public void ScenarioSetAddressLow(byte scenario)
	{
		byte[] ctrl = BuildCtrl(true, 4, OpScenario, scenario, 0, 0, 0, 0);
		byte[] resp = Exchange(ctrl, 4, "SCENARIO_SET_ADDR_LOW");
		Console.WriteLine("  SET addr={0} rsp: {1}", scenario,
			BitConverter.ToString(resp, 0, Math.Min(resp.Length, 8)));
	}

	public void ScenarioSetArg1(byte scenario)
	{
		byte[] ctrl = BuildCtrl(true, 4, OpScenario, 0, scenario, 0, 0, 0);
		byte[] resp = Exchange(ctrl, 4, "SCENARIO_SET_ARG1");
		Console.WriteLine("  SET arg1={0} rsp: {1}", scenario,
			BitConverter.ToString(resp, 0, Math.Min(resp.Length, 8)));
	}

	public void SetWaveform(ushort mode)
	{
		byte[] ctrl = BuildCtrl(true, 1, OpSetWaveform, 0, mode, 0, 0, 0);
		Exchange(ctrl, 1, "SET_WAVEFORM");
	}

	/*
	 * Windows packs 0xB3 into CDB args. E-multitouch: expect IN len 6 (flags 0x80).
	 * Older pen-mouse used no-IN; prefer expectIn=true for MT entry.
	 */
	public void DynamicCdb(ushort arg1, ushort arg2, ushort arg3, ushort arg4)
	{
		DynamicCdb(arg1, arg2, arg3, arg4, true);
	}

	public void DynamicCdb(ushort arg1, ushort arg2, ushort arg3, ushort arg4, bool expectIn)
	{
		if (expectIn)
		{
			byte[] ctrl = BuildCtrl(true, 6, OpDynamic, 0, arg1, arg2, arg3, arg4);
			byte[] resp = Exchange(ctrl, 6, "DYNAMIC_CDB");
			Console.WriteLine("  B3 {0:X4}/{1:X4}/{2:X4} rsp: {3}",
				arg1, arg2, arg3, BitConverter.ToString(resp, 0, Math.Min(resp.Length, 8)));
		}
		else
		{
			byte[] ctrl = BuildCtrl(false, 0, OpDynamic, 0, arg1, arg2, arg3, arg4);
			Exchange(ctrl, 0, "DYNAMIC_CDB");
		}
	}

	public byte[] ReadReg(uint reg)
	{
		byte[] ctrl = BuildCtrl(true, 4, OpReadReg, reg, 4, 0, 0, 0);
		byte[] resp = Exchange(ctrl, 4, "READ_REG");
		Console.WriteLine("  R 0x{0:X8} => {1}", reg, BitConverter.ToString(resp));
		return resp;
	}

	public void WriteReg(uint reg, byte[] be4)
	{
		if (be4 == null || be4.Length < 4)
			throw new ArgumentException("need 4 BE bytes");
		byte[] ctrl = BuildCtrl(false, 4, OpWriteReg, reg, 4, 0, 0, 0);
		ExchangeOutPayload(ctrl, be4, "WRITE_REG");
		Console.WriteLine("  W 0x{0:X8} <= {1}", reg, BitConverter.ToString(be4, 0, 4));
	}

	public void CmdExpect(byte op, uint addr, ushort a1, ushort a2, ushort a3, ushort a4,
		int inLen, string tag)
	{
		byte[] ctrl = BuildCtrl(inLen > 0, (uint)Math.Max(inLen, 0), op, addr, a1, a2, a3, a4);
		if (inLen > 0)
		{
			byte[] resp = Exchange(ctrl, inLen, tag);
			Console.WriteLine("  {0} op=0x{1:X2} rsp: {2}", tag, op,
				BitConverter.ToString(resp, 0, Math.Min(resp.Length, 16)));
		}
		else
			Exchange(ctrl, 0, tag);
	}

	/*
	 * Full E-multitouch early sequence (ops 1-20) from cold KB — no Homebar.
	 * Goal: GET byte1 == 3. Then user must confirm 2/3 finger (HID 0x0c).
	 */
	public byte MtEnterCold()
	{
		Console.WriteLine("=== mt-enter from cold (E-usbc-ops 1..20, no A8 blit) ===");
		byte before = ScenarioGet();
		Console.WriteLine("before scenario={0} (want start at 1=KB for clean test)", before);

		try { CmdExpect(OpReadMem, 0x00000080u, 0x0010, 0, 0, 0, 16, "81a"); }
		catch (Exception ex) { Console.WriteLine("  warn 81a: " + ex.Message); }

		try { DynamicCdb(0x0101, 0x0003, 0x0301, 0, true); }
		catch (Exception ex) { Console.WriteLine("  warn B3: " + ex.Message); }
		try { SetWaveform(0x200); } catch (Exception ex) { Console.WriteLine("  warn A9: " + ex.Message); }
		{
			byte[] ctrl = BuildCtrl(true, 4, OpScenario, 0x01030100u, 0, 0, 0, 0);
			byte[] resp = Exchange(ctrl, 4, "A6_MT1");
			Console.WriteLine("  A6 0x01030100 rsp: {0}", BitConverter.ToString(resp, 0, Math.Min(4, resp.Length)));
		}

		byte[] cfg = null;
		try { cfg = ReadReg(0x18001224u); } catch (Exception ex) { Console.WriteLine("  warn: " + ex.Message); }
		try { cfg = ReadReg(0x18001138u); } catch (Exception ex) { Console.WriteLine("  warn: " + ex.Message); }
		if (cfg != null && cfg.Length >= 4)
		{
			try { WriteReg(0x18001138u, cfg); }
			catch (Exception ex) { Console.WriteLine("  warn W cfg: " + ex.Message); }
		}

		try { CmdExpect(OpAe, 0, 0x0100, 0, 0, 0, 1, "AE"); }
		catch (Exception ex) { Console.WriteLine("  warn AE: " + ex.Message); }
		try { CmdExpect(OpSetHandwr, 0, 0, 0, 0, 0, 8, "AC"); }
		catch (Exception ex) { Console.WriteLine("  warn AC: " + ex.Message); }

		try { CmdExpect(OpReadMem, 0x00000080u, 0x0010, 0, 0, 0, 16, "81b"); }
		catch (Exception ex) { Console.WriteLine("  warn 81b: " + ex.Message); }

		try { DynamicCdb(0x0100, 0x0003, 0x0301, 0, true); }
		catch (Exception ex) { Console.WriteLine("  warn B3: " + ex.Message); }
		try { SetWaveform(0x200); } catch (Exception ex) { Console.WriteLine("  warn A9: " + ex.Message); }
		{
			byte[] ctrl = BuildCtrl(true, 4, OpScenario, 0x01030100u, 0, 0, 0, 0);
			byte[] resp = Exchange(ctrl, 4, "A6_MT2");
			Console.WriteLine("  A6 0x01030100 rsp: {0}", BitConverter.ToString(resp, 0, Math.Min(4, resp.Length)));
		}

		try { CmdExpect(OpReadMem, 0x00000080u, 0x0010, 0, 0, 0, 16, "81c"); }
		catch (Exception ex) { Console.WriteLine("  warn 81c: " + ex.Message); }

		try { DynamicCdb(0x0100, 0x0003, 0x0201, 0, true); }
		catch (Exception ex) { Console.WriteLine("  warn B3: " + ex.Message); }

		try
		{
			byte[] ctrl = BuildCtrl(true, 112, OpGetSys, 0x38393531u, 1, 2, 0, 0);
			byte[] resp = Exchange(ctrl, 112, "GET_SYS");
			Console.WriteLine("  GET_SYS {0} B head: {1}", resp.Length,
				BitConverter.ToString(resp, 0, Math.Min(16, resp.Length)));
		}
		catch (Exception ex) { Console.WriteLine("  warn GET_SYS: " + ex.Message); }

		try { ReadReg(0x18001224u); } catch (Exception ex) { Console.WriteLine("  warn: " + ex.Message); }
		try { cfg = ReadReg(0x18001138u); } catch (Exception ex) { Console.WriteLine("  warn: " + ex.Message); }
		if (cfg != null && cfg.Length >= 4)
		{
			try { WriteReg(0x18001138u, cfg); }
			catch (Exception ex) { Console.WriteLine("  warn W cfg: " + ex.Message); }
		}

		byte after = ScenarioGet();
		Console.WriteLine("=== after mt-enter: scenario={0} (WANT 3) ===", after);
		if (after == ScenarioPenMouse)
			Console.WriteLine("PASS GET=3. Now confirm 2/3 finger on E-Ink (Notepad LCD must stay clean).");
		else if (after == ScenarioNormal)
			Console.WriteLine("GOT GET=0 — bare leave class, NOT MT latch. Touch likely 0x90 only.");
		else if (after == ScenarioKeyboard)
			Console.WriteLine("STILL KB GET=1 — sequence did not leave keyboard.");
		else
			Console.WriteLine("Unexpected GET={0}", after);
		return after;
	}

	public bool TrySetScenario(byte want)
	{
		/* want==0 via <<24 is address 0 = GET; skip that encoding. */
		if (want != ScenarioNormal)
		{
			Console.WriteLine("Trying hi-byte address encoding (scenario<<24) for {0}...", want);
			ScenarioSetHiByte(want);
			byte got = ScenarioGet();
			Console.WriteLine("  GET => {0}", got);
			if (got == want)
				return true;
			/* C-penmouse: after leaving KB, GET reports 0 even when we asked for 3. */
			if (want == ScenarioPenMouse && got == ScenarioNormal)
			{
				Console.WriteLine("  note: firmware reports 0 after 0x03000000 (matches Homebar pen path)");
				return true;
			}
		}

		Console.WriteLine("Trying low address DWORD encoding for {0}...", want);
		ScenarioSetAddressLow(want);
		{
			byte got = ScenarioGet();
			Console.WriteLine("  GET => {0}", got);
			if (got == want)
				return true;
			if (want == ScenarioPenMouse && got == ScenarioNormal)
				return true;
		}

		if (want != ScenarioNormal)
		{
			Console.WriteLine("Trying arg1 encoding for scenario {0}...", want);
			ScenarioSetArg1(want);
			byte got = ScenarioGet();
			Console.WriteLine("  GET => {0}", got);
			if (got == want)
				return true;
			if (want == ScenarioPenMouse && got == ScenarioNormal)
				return true;
		}
		return false;
	}

	public void ClearTpSlot(byte index)
	{
		byte[] payload = new byte[20];
		payload[16] = index;
		payload[17] = 0; /* ITE8951_TP_AREA_NO_REPORT */
		byte[] ctrl = BuildCtrl(false, (uint)payload.Length, OpSetTpArea, 0, (ushort)payload.Length, 0, 0, 0);
		ExchangeOutPayload(ctrl, payload, "TP_AREA");
	}

	/*
	 * Leave firmware keyboard. C-penmouse: Homebar pen path GETs as 0, not 3.
	 * Wire hit that works here: A6 address 0x03000000 then GET byte1==0.
	 */
	public void PenMouse()
	{
		Console.WriteLine("Before: scenario GET...");
		byte before = ScenarioGet();
		Console.WriteLine("scenario={0}", before);

		Console.WriteLine("Dynamic 0xB3 pen-ish CDB (from C-penmouse)...");
		try { DynamicCdb(0x0100, 0x0103, 0x0301, 0); }
		catch (Exception ex) { Console.WriteLine("  warn: " + ex.Message); }

		Console.WriteLine("Waveform 0x200...");
		try { SetWaveform(0x200); } catch (Exception ex) { Console.WriteLine("  warn: " + ex.Message); }

		Console.WriteLine("Request pen-mouse via A6 address 0x03000000...");
		if (TrySetScenario(ScenarioPenMouse))
		{
			byte after = ScenarioGet();
			Console.WriteLine("OK: left keyboard (scenario now {0})", after);
			return;
		}

		Console.WriteLine("Fallback: try force scenario 0 via low DWORD...");
		ScenarioSetAddressLow(ScenarioNormal);
		byte got = ScenarioGet();
		Console.WriteLine("  GET => {0}", got);
		if (got != ScenarioKeyboard)
			Console.WriteLine("OK: left keyboard (scenario now {0})", got);
		else
			Console.WriteLine("WARNING: still in firmware keyboard scenario");
	}
}

internal static class Program
{
	static int Main(string[] args)
	{
		if (args.Length < 1)
		{
			Usage();
			return 1;
		}

		string cmd = args[0].ToLowerInvariant();
		try
		{
			using (IteDevice dev = IteDevice.Open())
			{
				switch (cmd)
				{
					case "get-sys":
					case "info":
					{
						byte[] r = dev.GetSys();
						Console.WriteLine("GET_SYS {0} bytes: {1}", r.Length, BitConverter.ToString(r, 0, Math.Min(32, r.Length)));
						if (r.Length >= 16)
						{
							uint sig = BitConverter.ToUInt32(r, 0);
							Console.WriteLine("signature LE=0x{0:X8}", sig);
						}
						break;
					}
					case "scenario-get":
					case "get-scenario":
					{
						byte s = dev.ScenarioGet();
						Console.WriteLine("scenario={0} (0=draw 1=keyboard 3=pen-mouse)", s);
						break;
					}
					case "scenario-set":
					case "set-scenario":
					{
						if (args.Length < 2)
						{
							Console.WriteLine("need scenario number");
							return 1;
						}
						byte want = byte.Parse(args[1]);
						bool ok = dev.TrySetScenario(want);
						Console.WriteLine(ok ? "latched" : "FAILED to latch");
						return ok ? 0 : 2;
					}
					case "pen-mouse":
						dev.PenMouse();
						break;
					case "mt-replay":
						dev.MtModeReplay();
						break;
					case "mt-enter":
					{
						byte s = dev.MtEnterCold();
						return s == IteDevice.ScenarioPenMouse ? 0 : 2;
					}
					case "fill":
					{
						byte gray = 0xff;
						if (args.Length >= 2)
						{
							string a = args[1].ToLowerInvariant();
							if (a == "black" || a == "0")
								gray = 0x00;
							else if (a == "white" || a == "ff" || a == "0xff")
								gray = 0xff;
							else if (a == "gray" || a == "80")
								gray = 0x80;
							else
								gray = Convert.ToByte(args[1], args[1].StartsWith("0x", StringComparison.OrdinalIgnoreCase) ? 16 : 10);
						}
						dev.FillScreen(gray);
						break;
					}
					case "stripes":
						dev.FillStripes();
						break;
					case "blit-test":
					{
						Console.WriteLine("=== blit-test: white ===");
						dev.FillScreen(0xff);
						Console.WriteLine("Look at E-Ink: what cleared? what ghosted? Enter...");
						Console.ReadLine();
						Console.WriteLine("=== blit-test: black ===");
						dev.FillScreen(0x00);
						Console.WriteLine("Look again. Enter...");
						Console.ReadLine();
						Console.WriteLine("=== blit-test: stripes ===");
						dev.FillStripes();
						Console.WriteLine("Done. Note ghosts / Homebar remnants / MT still live?");
						break;
					}
					default:
						Usage();
						return 1;
				}
			}
			return 0;
		}
		catch (Exception ex)
		{
			Console.Error.WriteLine("ERROR: " + ex.Message);
			return 1;
		}
	}

	static void Usage()
	{
		Console.WriteLine("EinkWinUsb — ITE8951 WinUSB tool (048d:8951 MI_00)");
		Console.WriteLine("  EinkWinUsb.exe info");
		Console.WriteLine("  EinkWinUsb.exe scenario-get");
		Console.WriteLine("  EinkWinUsb.exe scenario-set <0|1|3>");
		Console.WriteLine("  EinkWinUsb.exe pen-mouse");
		Console.WriteLine("  EinkWinUsb.exe mt-replay     # short E A6/B3 subset");
		Console.WriteLine("  EinkWinUsb.exe mt-enter      # full E ops 1-20 from cold KB");
		Console.WriteLine("  EinkWinUsb.exe fill [white|black|gray|0xNN]");
		Console.WriteLine("  EinkWinUsb.exe stripes");
		Console.WriteLine("  EinkWinUsb.exe blit-test     # white → black → stripes");
		Console.WriteLine("Stop EinkSvr first: Set-EinkSvrAutostart.ps1 -Mode Manual -StopNow");
	}
}
