******************************************************************************
* @file    readme.txt
* @author  MCD Application Team
* @version 6.0
* @date    10-October-2024  
* @brief   MB1381 Bill of materials package
******************************************************************************
* COPYRIGHT(c) 2024 STMicroelectronics
*
* The Open Platform License Agreement (“Agreement”) is a binding legal contract
* between you ("You") and STMicroelectronics International N.V. (“ST”), a
* company incorporated under the laws of the Netherlands acting for the purpose
* of this Agreement through its Swiss branch 39, Chemin du Champ des Filles,
* 1228 Plan-les-Ouates, Geneva, Switzerland.
*
* By using the enclosed reference designs, schematics, PC board layouts, and
* documentation, in hardcopy or CAD tool file format (collectively, the
* “Reference Material”), You are agreeing to be bound by the terms and
* conditions of this Agreement. Do not use the Reference Material until You
* have read and agreed to this Agreement terms and conditions. The use of
* the Reference Material automatically implies the acceptance of the Agreement
* terms and conditions.
*
* The complete Open Platform License Agreement can be found on www.st.com/opla.
******************************************************************************
* 6.0 - 10-October-2024
========================
    + Add MB1381-H745XI-B03 BOM with new part references:
	- U33 (LCD) ROCKTECH RK043FN48H-CT672B replaced by ROCKTECH RK043FN88H-CT661C with impact on firmware.
	- Several part references updated due to obsolescence (such as LCD or others).

    + Add MB1381-H745XI-B04 BOM with new part references:
	- U11 (eMMC) Micron Technology-MTFC4GACAJCN-1M WT replaced with new Memory KIOXIA-THGBMTG5D1LBAI with no impact on firmware.
        - Several part references updated due to obsolescence (such as memory or others).

    + Add MB1381-H75XB-B04 BOM with new part references:
	- U33 (TFT-LCD) ROCKTECH RK043FN48H-CT672B  replaced by ROCKTECH_RK043FN88H-CT661C with impact on firmware.
	- U11 (eMMC) Micron Technology-MTFC4GACAJCN-1M WT replaced with new Memory KIOXIA-THGBMTG5D1LBAI with no impact on firmware.
        - Several part references updated due to obsolescence (such as memory or others).

* 5.0 - 15-September-2023
========================
    + Removed version of MB1381-H750XB-B02_BOM which was never produced and directly switched to MB1381-H750XB-B04_BOM.

========================
* 4.0 - 03-August-2022
========================
    + Reworked package with consistent data and added versio B03.

========================
* 3.0 - 06-November-2020
========================
    + correct BOM B-02 format for both STM32H750B-DK and STM32H745I-DISCO.

========================
* 2.0 - 08-July-2020
========================
    + MB1381-B02 BOM added.
    + the configuration of SMPS related solder bridges is managed by variant of H75XB and H745XI. no change of real product.

========================
* 1.0 - 16-November-2018
========================
    + First official release

******************* (C) COPYRIGHT 2024 STMicroelectronics *****END OF FILE
