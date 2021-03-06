#include "RA_MemManager.h"

#include "RA_Core.h"
#include "RA_Achievement.h"

MemManager g_MemManager;

MemManager::MemManager()
 :	m_nComparisonSizeMode( ComparisonVariableSize::SixteenBit ),
	m_bUseLastKnownValue( true ),
	m_Candidates( nullptr )
{
}

//	virtual
MemManager::~MemManager()
{
	ClearMemoryBanks();
}

void MemManager::ClearMemoryBanks()
{
	m_Banks.clear();
	if( m_Candidates != nullptr )
	{
		delete[] m_Candidates;
		m_Candidates = nullptr;
	}
}

void MemManager::AddMemoryBank( size_t nBankID, _RAMByteReadFn* pReader, _RAMByteWriteFn* pWriter, size_t nBankSize )
{
	if( m_Banks.find( nBankID ) != m_Banks.end() )
	{
		ASSERT( !"Failed! Bank already added! Did you remove the existing bank?" );
		return;
	}
	
	m_Banks[ nBankID ].BankSize = nBankSize;
	m_Banks[ nBankID ].Reader = pReader;
	m_Banks[ nBankID ].Writer = pWriter;
}

void MemManager::Reset( unsigned short nSelectedMemBank, ComparisonVariableSize nNewVarSize )
{
	//	RAM must be installed for this to work!
	if( m_Banks.size() == 0 )
		return;

	if( m_Banks.find( nSelectedMemBank ) == m_Banks.end() )
		return;

	m_nActiveMemBank = nSelectedMemBank;
	m_nComparisonSizeMode = nNewVarSize;
	
	const size_t RAM_SIZE = m_Banks[ m_nActiveMemBank ].BankSize;
	
	if( m_Candidates == nullptr )
		m_Candidates = new MemCandidate[ RAM_SIZE*2 ];	//	To allow for upper and lower nibbles

	//	Initialize the memory cache: i.e. every memory address is valid!
	if( ( m_nComparisonSizeMode == Nibble_Lower ) ||
		( m_nComparisonSizeMode == Nibble_Upper ) )
	{
		for( size_t i = 0; i < RAM_SIZE; ++i )
		{
			m_Candidates[ ( i * 2 ) ].m_nAddr = i;
			m_Candidates[ ( i * 2 ) ].m_bUpperNibble = false;			//lower first?
			m_Candidates[ ( i * 2 ) ].m_nLastKnownValue = static_cast<unsigned int>( RAMByte( m_nActiveMemBank, i ) & 0xf );

			m_Candidates[ ( i * 2 ) + 1 ].m_nAddr = i;
			m_Candidates[ ( i * 2 ) + 1 ].m_bUpperNibble = true;
			m_Candidates[ ( i * 2 ) + 1 ].m_nLastKnownValue = static_cast<unsigned int>( ( RAMByte( m_nActiveMemBank, i ) >> 4 ) & 0xf );
		}
		m_nNumCandidates = RAM_SIZE * 2;
	}
	else if( m_nComparisonSizeMode == EightBit )
	{
		for( DWORD i = 0; i < RAM_SIZE; ++i )
		{
			m_Candidates[ i ].m_nAddr = i;
			m_Candidates[ i ].m_nLastKnownValue = RAMByte( m_nActiveMemBank, i );
		}
		m_nNumCandidates = RAM_SIZE;
	}
	else if( m_nComparisonSizeMode == SixteenBit )
	{
		for( DWORD i = 0; i < ( RAM_SIZE / 2 ); ++i )
		{
			m_Candidates[ i ].m_nAddr = ( i * 2 );
			m_Candidates[ i ].m_nLastKnownValue = 
				( RAMByte( m_nActiveMemBank, ( i * 2 ) ) ) | 
				( RAMByte( m_nActiveMemBank, ( i * 2 ) + 1 ) << 8 );
		}
		m_nNumCandidates = RAM_SIZE / 2;
	}
 	else if( m_nComparisonSizeMode == ThirtyTwoBit )
 	{
 		//	Assuming 32-bit-aligned! 		
		for( DWORD i = 0; i < ( RAM_SIZE / 4 ); ++i )
 		{
			m_Candidates[ i ].m_nAddr = ( i * 4 );
			m_Candidates[ i ].m_nLastKnownValue =
				( RAMByte( m_nActiveMemBank, ( i * 4 ) ) ) |
				( RAMByte( m_nActiveMemBank, ( i * 4 ) + 1 ) << 8 ) |
				( RAMByte( m_nActiveMemBank, ( i * 4 ) + 2 ) << 16 ) |
				( RAMByte( m_nActiveMemBank, ( i * 4 ) + 3 ) << 24 );
 		}
		m_nNumCandidates = RAM_SIZE / 4;
 	}
}

size_t MemManager::Compare( ComparisonType nCompareType, unsigned int nTestValue, bool& bResultsFound )
{
	size_t nGoodResults = 0;
	for( size_t i = 0; i < m_nNumCandidates; ++i )
	{
		unsigned int nAddr = m_Candidates[ i ].m_nAddr;
		unsigned int nLiveValue = 0;

		if( ( m_nComparisonSizeMode == Nibble_Lower ) || 
			( m_nComparisonSizeMode == Nibble_Upper ) )
		{
			if( m_Candidates[ i ].m_bUpperNibble )
				nLiveValue = ( RAMByte( m_nActiveMemBank, nAddr ) >> 4 ) & 0xf;
			else
				nLiveValue = RAMByte( m_nActiveMemBank, nAddr ) & 0xf;
		}
		else if( m_nComparisonSizeMode == EightBit )
		{
			nLiveValue = RAMByte( m_nActiveMemBank, nAddr );
		}
		else if( m_nComparisonSizeMode == SixteenBit )
		{
			nLiveValue = RAMByte( m_nActiveMemBank, nAddr );
			nLiveValue |= ( RAMByte( m_nActiveMemBank, nAddr + 1 ) << 8 );
		}
 		else if( m_nComparisonSizeMode == ThirtyTwoBit )
 		{
			nLiveValue = RAMByte( m_nActiveMemBank, nAddr );
			nLiveValue |= ( RAMByte( m_nActiveMemBank, nAddr + 1 ) << 8 );
			nLiveValue |= ( RAMByte( m_nActiveMemBank, nAddr + 2 ) << 16 );
			nLiveValue |= ( RAMByte( m_nActiveMemBank, nAddr + 3 ) << 24 );
 		}
		
		bool bValid = false;
		unsigned int nQueryValue = ( m_bUseLastKnownValue ? m_Candidates[ i ].m_nLastKnownValue : nTestValue );
		switch( nCompareType )
		{
		case Equals:				bValid = ( nLiveValue == nQueryValue );	break;
		case LessThan:				bValid = ( nLiveValue < nQueryValue );	break;
		case LessThanOrEqual:		bValid = ( nLiveValue <= nQueryValue );	break;
		case GreaterThan:			bValid = ( nLiveValue > nQueryValue );	break;
		case GreaterThanOrEqual:	bValid = ( nLiveValue >= nQueryValue );	break;
		case NotEqualTo:			bValid = ( nLiveValue != nQueryValue );	break;
		default:
			ASSERT( !"Unknown comparison type?!" );
			break;
		}

		//	If the current address in ram still matches the query, store in result[]
		if( bValid )
		{
			//	Optimisation: just store it back in m_Candidates
			m_Candidates[ nGoodResults ].m_nLastKnownValue = nLiveValue;
			m_Candidates[ nGoodResults ].m_nAddr = m_Candidates[ i ].m_nAddr;
			m_Candidates[ nGoodResults ].m_bUpperNibble = m_Candidates[ i ].m_bUpperNibble;
			nGoodResults++;
		}
	}
	
	//	If we have any results, this is good :)
	bResultsFound = ( nGoodResults > 0 );
	if( bResultsFound )
		m_nNumCandidates = nGoodResults;

	return m_nNumCandidates;
}

void MemManager::ChangeActiveMemBank( unsigned short nMemBank )
{
	if( m_Banks.find( nMemBank ) == m_Banks.end() )
	{
		ASSERT( !"Cannot find memory bank!" );
		return;
	}
	
	if( m_Candidates != nullptr )
		delete[] m_Candidates;

	Reset( nMemBank, m_nComparisonSizeMode );
}
