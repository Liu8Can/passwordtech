�
 TPASSWENTERDLG 0Y  TPF0TPasswEnterDlgPasswEnterDlgLeftoTopnBorderIconsbiSystemMenu CaptionPasswordClientHeightClientWidthColor	clBtnFaceFont.CharsetANSI_CHARSET
Font.ColorclWindowTextFont.Height�	Font.NameTahoma
Font.Style OldCreateOrder	
OnActivateFormActivateOnShowFormShow
DesignSize PixelsPerInch`
TextHeight TLabelPasswLblLeftTopWidthZHeightAnchorsakLeftakTopakRight CaptionEnter password:Font.CharsetANSI_CHARSET
Font.ColorclWindowTextFont.Height�	Font.NameTahoma
Font.StylefsBold 
ParentFont  TLabelConfirmPasswLblLeftTop@WidthZHeightCaptionConfirm password:Color	clBtnFaceParentColor  TSpeedButtonTogglePasswBtnLeft� TopWidthHeightHintHide/show password
AllowAllUp	AnchorsakTopakRight 
GroupIndexCaption   � � � Flat	Font.CharsetSYMBOL_CHARSET
Font.ColorclMaroonFont.Height�	Font.NameSymbol
Font.Style 
ParentFontParentShowHintShowHint	OnClickTogglePasswBtnClick  TLabel
KeyFileLblLeftTop� Width� HeightCaptionKey file for password database:Font.CharsetANSI_CHARSET
Font.ColorclWindowTextFont.Height�	Font.NameTahoma
Font.StylefsBold 
ParentFont  TSpeedButton	BrowseBtnLeft� Top� WidthHeightHintBrowseAnchorsakTopakRight 
ImageIndex	ImageName
007-folderImagesMainForm.ImageList16ParentShowHintShowHint	OnClickBrowseBtnClick  TSpeedButtonCreateKeyFileBtnLeft� Top� WidthHeightHintCreate key fileAnchorsakTopakRight 
ImageIndex	ImageNamefloppy-diskImagesMainForm.ImageList16ParentShowHintShowHint	OnClickCreateKeyFileBtnClick  	TCheckBoxOldVersionCheckLeft� Top� WidthaHeightAnchorsakTopakRight CaptionPWGen <2.3.0TabOrder  TButtonOKBtnLeftvTop� WidthKHeightAnchorsakTopakRight CaptionOKDefault	TabOrderOnClick
OKBtnClick  TButton	CancelBtnLeft� Top� WidthKHeightAnchorsakTopakRight Cancel	CaptionCancelModalResultTabOrder  TEditPasswBoxLeftTopWidth� HeightAnchorsakLeftakTopakRight Font.CharsetANSI_CHARSET
Font.ColorclWindowTextFont.Height�	Font.NameTahoma
Font.Style 
ParentFontTabOrder   TEditConfirmPasswBoxLeftTopSWidth� HeightAnchorsakLeftakTopakRight Font.CharsetANSI_CHARSET
Font.ColorclWindowTextFont.Height�	Font.NameTahoma
Font.Style 
ParentFontTabOrder  	TCheckBoxRememberPasswCheckLeftToprWidth� HeightCaptionRemember password (seconds):TabOrderOnClickRememberPasswCheckClick  TEditRememberPasswTimeBoxLeft/Top� Width1HeightEnabledTabOrderText10  TUpDownRememberPasswTimeSpinBtnLeft`Top� WidthHeight	AssociateRememberPasswTimeBoxEnabledMax�Position
TabOrder  	TComboBox
KeyFileBoxLeftTop� Width� HeightStylecsDropDownListAnchorsakLeftakTopakRight 	ItemIndex TabOrderText(None)Items.Strings(None)   TTimerKeyExpiryTimerEnabledOnTimerKeyExpiryTimerTimerLeftTop�   TOpenDialogOpenDlgLeftHTop�   TSaveDialogSaveDlgOptionsofOverwritePromptofHideReadOnlyofEnableSizing Left(Top�    