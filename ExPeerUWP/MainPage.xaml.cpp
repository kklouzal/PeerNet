//
// MainPage.xaml.cpp
// Implementation of the MainPage class.
//

#include "pch.h"
#include "MainPage.xaml.h"

using namespace ExPeerUWP;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;


#include <locale>
#include <codecvt>
#include <string>

Platform::String^ stringToPlatformString(std::string inputString) {
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	std::wstring intermediateForm = converter.from_bytes(inputString);
	Platform::String^ retVal = ref new Platform::String(intermediateForm.c_str());

	return retVal;
}

// The Blank Page item template is documented at https://go.microsoft.com/fwlink/?LinkId=402352&clcid=0x409

MainPage^ rootPage;

void MyLogger::Log(std::string strOut)
{
	rootPage->logOut(stringToPlatformString(strOut));
}

MainPage::MainPage()
{
	InitializeComponent();
	rootPage = this;
}



void ExPeerUWP::MainPage::Button_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	PeerNet::Initialize(&LogClass);
	Socket = PeerNet::LoopBack();
	Peer = PeerNet::LocalHost();
}


void ExPeerUWP::MainPage::Button_Click_1(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	//	1. Delete all your peers
	if (Peer != nullptr && Peer != PeerNet::LocalHost())
	{
		delete Peer;
	}
	//	2. Delete all your sockets
	if (Socket != nullptr && Socket != PeerNet::LoopBack())
	{
		delete Socket;
	}
	//	3. Shutdown PeerNet
	PeerNet::Deinitialize();
}


void ExPeerUWP::MainPage::Button_Click_2(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	if (Peer != nullptr)
	{
		unsigned int i = 0;
		while (i < 1024)
		{
			auto NewPacket = Peer->CreateNewPacket(PeerNet::PacketType::PN_Ordered);
			NewPacket->WriteData<std::string>("I'm about to be serialized and I'm ordered!!");
			Peer->Send_Packet(NewPacket.get());
			i++;
		}
	}
}


void ExPeerUWP::MainPage::Button_Click_3(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	if (Peer != nullptr)
	{
		unsigned int i = 0;
		while (i < 1024)
		{
			auto NewPacket = Peer->CreateNewPacket(PeerNet::PacketType::PN_Reliable);
			NewPacket->WriteData<std::string>("I'm about to be serialized and I'm reliable!!");
			Peer->Send_Packet(NewPacket.get());
			i++;
		}
	}
}


void ExPeerUWP::MainPage::Button_Click_4(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	if (Peer != nullptr)
	{
		unsigned int i = 0;
		while (i < 1024)
		{
			auto NewPacket = Peer->CreateNewPacket(PeerNet::PacketType::PN_Unreliable);
			NewPacket->WriteData<std::string>("I'm about to be serialized and I'm unreliable!!");
			Peer->Send_Packet(NewPacket.get());
			i++;
		}
	}
}


void ExPeerUWP::MainPage::Button_Click_5(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	//
	//	Establish the remote connection
	std::wstring wsTempHost(SocketHost->Text->Begin());
	std::wstring wsTempPort(SocketPort->Text->Begin());
	Peer = PeerNet::ConnectPeer(
		std::string(wsTempHost.begin(), wsTempHost.end()),
		std::string(wsTempPort.begin(), wsTempPort.end()),
		Socket);
}


void ExPeerUWP::MainPage::Button_Click_6(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	//
	//	We must open a visible conenction to establish a remote connection
	std::wstring wsTempHost(LocalHost->Text->Begin());
	std::wstring wsTempPort(LocalPort->Text->Begin());
	Socket = PeerNet::OpenSocket(
		std::string(wsTempHost.begin(), wsTempHost.end()),
		std::string(wsTempPort.begin(), wsTempPort.end()));
}
