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
		init_glthread(&first_vm_page_for_families->vm_page_family[0].free_block_priority_list_head);
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
	vm_page_family_curr->first_page = NULL;
	init_glthread(&first_vm_page_for_families->vm_page_family[0].free_block_priority_list_head);
	
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
	init_glthread(&vm_page->block_meta_data.priority_thread_glue);
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
	vm_page_family_t *vm_page_family = vm_page->page_family;
	
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


static int
free_blocks_comparision_function(
		void *_block_meta_data1,
		void *_block_meta_data2){
	
	block_meta_data_t *block_meta_data1 = (block_meta_data_t *) block_meta_data1;
	block_meta_data_t *block_meta_data2 = (block_meta_data_t *) block_meta_data2;
	
	if(block_meta_data1->block_size > block_meta_data2->block_size) 
		return -1;
	else if((block_meta_data1->block_size < block_meta_data2->block_size)
		return 1;
	return 0;
}									

static void
mm_add_free_block_meta_data_to_free_block_list(
					vm_page_family_t *vm_page_family,
					block_meta_data_t *free_block){
					
	assert(free_block->is_free == MM_TRUE);
	
	glthread_priority_insert(&vm_page_family->free_block_priority_list_head,
				&free_block->priority_thread_glue,
				free_blocks_comparision_function,
				offset_of(block_meta_data_t, priority_thread_glue));

}


/*The public function to be invoked by the application for Dynamic Memory Allocation*/
void * 
xcalloc(char *srtuct_name, int units){

	/*Step 1*/
	vm_page_family_t *pg_family = 
			lookup_page_family_by_name(struct_name);
	
	if(!pg_family){
		printf("Error : Structure %s is not registered with Memory Manager\n"
				struct_name);
		return NULL;
	}
	
	if(units * pg_family->struct_size > MAX_PAGE_ALLOCATE_MEMORY(1)){
		
		printf("Error : Memory Requested Exceeds Page Size\n");
		return NULL;
	}
	
	/*Find the page which can satisfy the request*/
	block_meta_data_t *free_block_meta_data = NULL;
	
	free_block_meta_data = mm_allocate_free_data_block(
					pg_family, unit * pg_family->struct_size);
					
	if(free_block_meta_data){
		memset((char *)(free_block_meta_data + 1), 0,
			free_block_meta_data->block_size);
		return (void *)(free_block_meta_data + 1);
	}
	
	return NULL;
	
}
/*
block_meta_data_t *
mm_get_biggest_free_block_page_family(vm_page_family){
	
}
*/

/*Fn to mark block_meta_data as being Allocated for
  'size' bytes of application data. Return TRUE if
  block allocation succeeds						*/
  
static vm_bool_t
mm_split_free_data_block_for_allocation(
					vm_page_family_t *vm_page_family,
					block_meta_data_t *block_meta_data,
					uint32_t size){
	
	block_meta_data_t *next_block_meta_data = NULL;
	
	assert(block_meta_data->is_free == MM_TRUE);
	
	if(block_meta_data->block_size < size){
		return MM_FALSE;
	}
	
	uint32_t remaining_size = 
				block_meta_data->block_size - size;
	
	block_meta_data->is_free = MM_FALSE;
	block_meta_data->block_size = size;
	remove_glthread(&block_meta_data->priority_thread_glue);
	/*block_meta_data->offset remains unchanged*/
	
	/*Case 1: No split*/
	if(!remaining_size){
		return MM_TRUE;	
	}
	
	/*Case 3: Partial Split : Soft Internal Fragmentation*/
	else if(sizeof(block_meta_data_t) < remaining_size &&
				remaining_size < (sizeof(block_meta_data_t) + vm_page_family->struct_size)){
		/*New Meta block is to be created*/
		next_block_meta_data = NEXT_META_BLOCK_BY_SIZE(block_meta_data);
		next_block_meta_data->free = MM_TRUE;
		next_block_meta_data->block_size = 
				remaining_size - sizeof(block_meta_data_t);
		next_block_meta_data->offset = block_meta_data->offset + 
				sizeof(block_meta_data_t) + block_meta_data->block_size;
				
		/*Insert new free block into priority queue*/
		init_glthread(&next_block_meta_data->priority_thread_glue);
		mm_add_free_block_meta_data_to_free_block_list(
					vm_page_family, next_block_meta_data);
		
		/*To fix up linkages*/
		mm_bind_blocks_for_allocation(block_meta_data, next_block_meta_data);
		
	}
	
	/*Case 3: Partial Split : Hard Internal Fragmentation*/
	else if(remaining_size < sizeof(block_meta_data_t)){
		/*No need to do anything !!*/
	}
	
	/*Case 2: Full split : New Meta block is created*/
	else{
		/*New Meta block is to be created*/
		next_block_meta_data = NEXT_META_BLOCK_BY_SIZE(block_meta_data);
		next_block_meta_data->free = MM_TRUE;
		next_block_meta_data->block_size = 
				remaining_size - sizeof(block_meta_data_t);
		next_block_meta_data->offset = block_meta_data->offset + 
				sizeof(block_meta_data_t) + block_meta_data->block_size;
				
		/*Insert new free block into priority queue*/
		init_glthread(&next_block_meta_data->priority_thread_glue);
		mm_add_free_block_meta_data_to_free_block_list(
					vm_page_family, next_block_meta_data);
		
		/*To fix up linkages*/
		mm_bind_blocks_for_allocation(block_meta_data, next_block_meta_data);
	}
	
	return MM_TRUE;
	

}



static block_meta_data_t *
mm_allocate_free_data_block(
		vm_page_family_t *vm_page_family,
		uint32_t req_size){
	
	vm_bool_t status = MM_FALSE;
	vm_page_t *vm_page = NULL;
	block_meta_data_t *block_meta_data = NULL;
	
	block_meta_data_t *biggest_block_meta_data = 
		mm_get_biggest_free_block_page_family(vm_page_family);
	
	if(!biggest_block_meta_data ||
			biggest_block_meta_data->block_size < req_size){
		
		/*Time to add a new ppage to page family to satisfy the request*/
		vm_page = mm_family_new_page_add(vm_page_family);
		
		/*Allocate the free block from this page now*/
		status = mm_split_free_data_block_for_allocation(vm_page_family,
				&vm_page->block_meta_data, req_size);
				
		if(status)
			return &vm_page->block_meta_data;
		
		return NULL;
	}
	
	/*The biggest block meta data can satisfy the request*/
	if(biggest_block_meta_data){
		status = mm_split_free_data_block_for_allocation(vm_page_family,
				biggest_block_meta_data, req_size);
	}
	
	if(status)
		return biggest_block_meta_data;
	
	return NULL;
	
}



