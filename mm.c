#include <stdio.h>
#include <memory.h>
#include <unistd.h>     /*for getpagesize*/
#include <sys/mman.h>   /*for using mmap()*/
#include <assert.h>
#include "mm.h"

static vm_page_for_families_t *first_vm_page_for_families = NULL;
static size_t SYSTEM_PAGE_SIZE = 0;

void mm_init()
{
	SYSTEM_PAGE_SIZE = getpagesize();
}

/*Function to request VM page from kernel*/
static void * mm_get_new_vm_page_from_kernel(int units){
	
	char *vm_page = mmap(
		0,
		units * SYSTEM_PAGE_SIZE,
		PROT_READ|PROT_WRITE|PROT_EXEC,
		MAP_ANON|MAP_PRIVATE,
		0, 0);
	
	if(vm_page == MAP_FAILED){
		printf("Error : VM Page allocation Failed\n");
		return NULL;
	}
	memset(vm_page, 0, units * SYSTEM_PAGE_SIZE);
	return (void *)vm_page;
}

/*Function to return a page kernel*/

static void mm_return_vm_page_to_kernel(void *vm_page, int units){
	if(munmap(vm_page, units * SYSTEM_PAGE_SIZE)){
		printf("Error : Could not munmap VM page to kernel");
	}
}

void mm_instantiate_new_page_family(char *struct_name, uint32_t struct_size){
	
	vm_page_family_t *vm_page_family_curr = NULL;
	vm_page_for_families_t *new_vm_page_for_families = NULL;
	
	if(struct_size > SYSTEM_PAGE_SIZE) {
		printf("Error : %s() Structure %s Size exceeds system page size\n",
			__FUNCTION__, struct_name);
		return;
	}
	
	if(!first_vm_page_for_families){
	
		first_vm_page_for_families = 
				(vm_page_for_families_t *)mm_get_new_vm_page_from_kernel(1);
		first_vm_page_for_families->next = NULL;
		strncpy(first_vm_page_for_families->vm_page_family[0].struct_name,
		struct_name, MM_MAX_STRUCT_NAME);
		first_vm_page_for_families->vm_page_family[0].struct_size = struct_size;
		return;
	}
	
	uint32_t count = 0;
	
	ITERATE_PAGE_FAMILIES_BEGIN(first_vm_page_for_families, vm_page_family_curr){
				
		if(strcmp(vm_page_family_curr->struct_name, struct_name) !=0){
			count++;
			continue;
		}	
		
		assert(0);	
		
	} ITERATE_PAGE_FAMILIES_END(first_vm_for_families, vm_page_family_curr);
	
	if(count == MAX_FAMILIES_PER_VM_PAGE){
		
		new_vm_page_for_families = 
			(vm_page_for_families_t *)mm_get_new_vm_page_from_kernel(1);
		new_vm_page_for_families->next = first_vm_page_for_families;
		first_vm_page_for_families = new_vm_page_for_families;
		vm_page_family_curr = &first_vm_page_for_families->vm_page_family[0];
	}
	
	strncpy(vm_page_family_curr->struct_name, struct_name, MM_MAX_STRUCT_NAME);
	vm_page_family_curr->struct_size = struct_size;
	
}


static void mm_union_free_blocks(block_meta_data_t *first, block_meta_data_t *second)
{
	assert(first->is_free == MM_TRUE && second->is_free == MM_TRUE);
	
	first->block_size += sizeof(block_meta_data_t) + second->block_size;
	
	first->next_block = second->next_block;
	
	if(second->next_block)
		second->next_block->prev_block = first;
}


void mm_print_registered_page_families(){

	vm_page_family_t *vm_page_family_curr = NULL;
	
	ITERATE_PAGE_FAMILIES_BEGIN(first_vm_page_for_families, vm_page_family_curr){
				
		
		printf("Page Family : %s ,Size = %d \n",vm_page_family_curr->struct_name,	\
				vm_page_family_curr->struct_size);
			
		
	} ITERATE_PAGE_FAMILIES_END(first_vm_for_families, vm_page_family_curr);
	
}

vm_page_family_t * lookup_page_family_by_name (char *struct_name)
{
	vm_page_family_t *vm_page_family_curr = NULL;
	
	ITERATE_PAGE_FAMILIES_BEGIN(first_vm_page_for_families, vm_page_family_curr)
	{
			
		if(strcmp(vm_page_family_curr->struct_name, struct_name) == 0)
		{
			return vm_page_family_curr;
		}
		
	} ITERATE_PAGE_FAMILIES_END(first_vm_for_families, vm_page_family_curr);
	
	return NULL;
}


vm_bool_t mm_is_vm_page_empty(vm_page_t *vm_page){
	if(vm_page->block_meta_data.next_block == NULL && 
		vm_page->block_meta_data.prev_block == NULL &&
		vm_page->block_meta_data.is_free == MM_TRUE){
			
			return MM_TRUE;
		}
	return MM_FALSE;
} 


static inline uint32_t mm_max_page_allocatable_memory(int units){
	return (uint32_t) ((SYSTEM_PAGE_SIZE * units) - offset_of(vm_page_t, page_memory));
				
}


vm_page_t *
allocate_vm_page(vm_page_family_t *vm_page_family){
	
	vm_page_t *vm_page = mm_get_new_vm_page_from_kernel(1);
	
	/*Initialize lower most Meta block of the VM page*/
	MARK_VM_PAGE_EMPTY(vm_page);
	
	vm_page->block_meta_data.block_size = mm_max_page_allocatable_memory(1);
	
	vm_page->block_meta_data.offset = offset_of(vm_page_t, block_meta_data);
	
	vm_page->next = NULL;
	vm_page->prev = NULL;
	
	/*Set the back pointer to page family*/
	vm_page->page_family = vm_page_family;
	
	/*If it is a first VM data page for a given page family*/
	if(!vm_page_family->first_page){
		vm_page_family->first_page = vm_page;
		return vm_page;
	}
	
	/* Insert new VM page to the head of the linked list*/
	vm_page->next = vm_page_family->first_page;
	vm_page_family->first_page = vm_page;
	vm_page_family->first_page->prev = vm_page;
}

void mm_vm_page_delete_and_free(vm_page_t *vm_page){
	vm_page_family_t *vm_page_family = vm_page->pg_family;
	
	/*If the page being deleted is the head of the linked list*/
	if(vm_page_family->first_page == vm_page)
	{
		vm_page_family->first_page = vm_page->next;
		if(vm_page->next)
			vm_page->next->prev = NULL;
		vm_page->next = NULL;
		vm_page->prev = NULL;
		mm_return_vm_page_to_kernel((void *)vm_page, 1);
		return;
	}
	
	/*If we are deleting the page from milddle or end of linked list*/
	if(vm_page->next)
		vm_page->next->prev = vm_page->prev;
	vm_page->prev->next = vm_page->next;
	mm_return_vm_page_to_kernel((void *)vm_page, 1);
}

