#include <salviar/include/async_renderer.h>

#include <salviar/include/renderer.h>
#include <salviar/include/renderer_impl.h>
#include <eflib/include/memory/bounded_buffer.h>
#include <eflib/include/diagnostics/assert.h>

#include <eflib/include/platform/boost_begin.h>
#include <boost/utility/addressof.hpp>
#include <boost/thread/mutex.hpp>
#include <eflib/include/platform/boost_end.h>

using std::vector;

BEGIN_NS_SALVIAR();

/* TODO Prototypes

struct vb_desc
{
	buffer_ptr	vert_buf;
	size_t		stride;
	size_t		offset;
};

struct ia_context
{
	buffer_ptr				index_buffer;
	format					index_format;
		
	primitive_topology		prim_topo;
		
	size_t					start_slot;
	vector<vb_desc>			vb_descs;
		
	input_layout_ptr		layout;
	
	vs_input_op*			vs_input_ops_;
};

struct rast_context
{
	raster_state_ptr		rast_state;
	vs_output_op*			vso_ops;
	viewport				vp;
};

struct vs_context
{
	cpp_vertex_shader_ptr				cpp_vs;
	vertex_shader_unit_ptr				vs_proto;
	shader_object_ptr					vx_shader;
	
	unordered_map<string, sampler_ptr>	samplers;
};

struct ps_context
{
	cpp_pixel_shader_ptr				cpp_ps;
	pixel_shader_unit_ptr				ps_proto;
	shader_object_ptr					px_shader;
	unordered_map<string, sampler_ptr>	samplers;
};

struct blending_context
{
	depth_stencil_state_ptr		ds_state;
	int32_t						stencil_ref;
	cpp_blend_shader_ptr		cpp_bs;
		
	vector<surface_ptr>			color_targets;
	surface_ptr					ds_target;
};

struct render_context
{
	ia_context				ia_ctx;
	rast_context			rast_ctx;
	vs_context				vs_ctx;
	ps_context				ps_ctx;
	blending_context		blending_ctx;
};

bool is_free()
{
	return context_queue.empty();
}

void draw_to_queue()
{
	check_context();
	context_queue.push_back(current_context);
	current_context = free_context_queue.pop_back();
}

void delivery_drawing()
{
	while(!waiting_exit)
	{
		context = context_queue.pop_back();
		render_board board;
		context->delivery(board);
		render_dev->draw(board);
		free_context_queue.push_back(context);
	}
}
*/
EFLIB_DECLARE_CLASS_SHARED_PTR(renderer);

class async_renderer: public renderer
{
public:
	async_renderer(): cmds_(32) {}
	~async_renderer()
	{
		release();
	}

	//inherited
	virtual result set_input_layout(input_layout_ptr const& layout)
	{
		boost::function<result()> cmd = boost::bind(&renderer::set_input_layout, impl_, layout);
		cmds_.push_front(cmd);
		return result::ok;
	}

	virtual result set_vertex_buffers(
		size_t starts_slot,
		size_t buffers_count, buffer_ptr const* buffers,
		size_t const* strides, size_t const* offsets
		)
	{
		vector<buffer_ptr>	buffers_vector(buffers, buffers+buffers_count);
		vector<size_t>		strides_vector(strides, strides+buffers_count);
		vector<size_t>		offsets_vector(offsets, offsets+buffers_count);

		boost::function<result()> cmd = boost::bind(
			&async_renderer::set_vertex_buffer_impl,
			this, starts_slot, buffers_vector, strides_vector, offsets_vector);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result set_index_buffer(buffer_ptr const& hbuf, format index_fmt)
	{
		boost::function<result()> cmd = boost::bind(
			&renderer::set_index_buffer, impl_, hbuf, index_fmt );
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result set_primitive_topology(primitive_topology primtopo)
	{
		boost::function<result()> cmd = boost::bind(
			&renderer::set_primitive_topology, impl_, primtopo );
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result set_vertex_shader(cpp_vertex_shader_ptr const& hvs)
	{
		vertex_shader_ = hvs;

		boost::function<result()> cmd = boost::bind(
			&renderer::set_vertex_shader, impl_, hvs );
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result set_vertex_shader_code( boost::shared_ptr<shader_object> const& vsc )
	{
		vertex_shader_code_ = vsc;
		boost::function<result()> cmd = boost::bind(
			&renderer::set_vertex_shader_code, impl_, vsc );
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result set_vs_variable_value( std::string const& name, void const* pvariable, size_t sz )
	{
		char const* pbytes = static_cast<char const*>(pvariable);
		vector<char> data_vector(pbytes, pbytes+sz);
		boost::function<result()> cmd =
			boost::bind(&async_renderer::set_vs_variable_value_impl, this, name, data_vector);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result set_vs_variable_pointer( std::string const& name, void const* pvariable, size_t sz )
	{
		char const* pbytes = static_cast<char const*>(pvariable);
		vector<char> data_vector(pbytes, pbytes+sz);
		boost::function<result()> cmd =
			boost::bind(&async_renderer::set_vs_variable_pointer_impl, this, name, data_vector);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result set_vs_sampler( std::string const& name, sampler_ptr const& samp )
	{
		boost::function<result()> cmd =
			boost::bind(&renderer::set_vs_sampler, impl_, name, samp);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result set_rasterizer_state(raster_state_ptr const& rs)
	{
		boost::function<result()> cmd = boost::bind(&renderer::set_rasterizer_state, impl_, rs);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result set_depth_stencil_state(depth_stencil_state_ptr const& dss, int32_t stencil_ref)
	{
		boost::function<result()> cmd = boost::bind(&renderer::set_depth_stencil_state, impl_, dss, stencil_ref);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result set_pixel_shader(cpp_pixel_shader_ptr const& hps)
	{
		pixel_shader_ = hps;

		boost::function<result()> cmd = boost::bind(&renderer::set_pixel_shader, impl_, hps);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result set_pixel_shader_code( boost::shared_ptr<shader_object> const& psc )
	{
		pixel_shader_code_ = psc;

		boost::function<result()> cmd = boost::bind(&renderer::set_pixel_shader_code, impl_, psc);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result set_ps_variable( std::string const& name, void const* data, size_t sz )
	{
		char const* pbytes = static_cast<char const*>(data);
		vector<char> data_vector(pbytes, pbytes+sz);
		boost::function<result()> cmd = boost::bind(&async_renderer::set_ps_variable_impl, this, name, data_vector);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result set_ps_sampler( std::string const& name, sampler_ptr const& samp )
	{
		boost::function<result()> cmd = boost::bind(&renderer::set_ps_sampler, impl_, name, samp);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result set_blend_shader(cpp_blend_shader_ptr const& hbs)
	{
		boost::function<result()> cmd = boost::bind(&renderer::set_blend_shader, impl_, hbs);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result set_viewport(const viewport& vp)
	{
		boost::function<result()> cmd = boost::bind(&renderer::set_viewport, impl_, vp);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result set_framebuffer_size(size_t width, size_t height, size_t num_samples)
	{
		boost::function<result()> cmd = boost::bind(&renderer::set_framebuffer_size, impl_, width, height, num_samples);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result set_framebuffer_format(pixel_format pxfmt)
	{
		boost::function<result()> cmd = boost::bind(&renderer::set_framebuffer_format, impl_, pxfmt);
		cmds_.push_front(cmd);

		return result::ok;
	};

	virtual result set_render_target_available(render_target tar, size_t target_index, bool valid)
	{
		boost::function<result()> cmd =
			boost::bind(&renderer::set_render_target_available, impl_, tar, target_index, valid);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result set_render_target(render_target tar, size_t target_index, surface_ptr const& surf)
	{
		boost::function<result()> cmd =
			boost::bind(&renderer::set_render_target, impl_, tar, target_index, surf);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result draw(size_t startpos, size_t primcnt)
	{
		boost::function<result()> cmd =
			boost::bind(&renderer::draw, impl_, startpos, primcnt);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result draw_index(size_t startpos, size_t primcnt, int basevert)
	{
		boost::function<result()> cmd =
			boost::bind(&renderer::draw_index, impl_, startpos, primcnt, basevert);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result clear_color(size_t target_index, const color_rgba32f& c)
	{
		boost::function<result()> cmd =
			boost::bind(&renderer::clear_color, impl_, target_index, c);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result clear_depth(float d)
	{
		boost::function<result()> cmd = 
			boost::bind(&renderer::clear_depth, impl_, d);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result clear_stencil(uint32_t s)
	{
		boost::function<result()> cmd = 
			boost::bind(&renderer::clear_stencil, impl_, s);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result clear_color(size_t target_index, const eflib::rect<size_t>& rc, const color_rgba32f& c)
	{
		boost::function<result()> cmd = 
			boost::bind(&renderer::clear_color, impl_, target_index, rc, c);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result clear_depth(const eflib::rect<size_t>& rc, float d)
	{
		boost::function<result()> cmd = 
			boost::bind(&renderer::clear_depth, impl_, rc, d);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result clear_stencil(const eflib::rect<size_t>& rc, uint32_t s)
	{
		boost::function<result()> cmd = 
			boost::bind(&renderer::clear_stencil, impl_, rc, s);
		cmds_.push_front(cmd);

		return result::ok;
	}

	virtual result flush()
	{
		// Waiting until command buffer is empty.
		boost::mutex::scoped_lock locker(waiting_mutex_);
		boost::function<result()> cmd = boost::bind(&async_renderer::flush_impl, this);
		cmds_.push_front(cmd);
		waiting_condition_.wait(locker);
		return result::ok;
	}

	virtual result present()
	{
		// Waiting until this frame is presented.
		boost::mutex::scoped_lock locker(waiting_mutex_);
		boost::function<result()> cmd = boost::bind(&async_renderer::present_impl, this);
		cmds_.push_front(cmd);
		waiting_condition_.wait(locker);
		return result::ok;
	}
	
	// Resources
	virtual input_layout_ptr create_input_layout(
		input_element_desc const* elem_descs, size_t elems_count,
		shader_object_ptr const& vs )
	{
		return impl_->create_input_layout(elem_descs, elems_count, vs);
	}
	
	virtual input_layout_ptr create_input_layout(
		input_element_desc const* elem_descs, size_t elems_count,
		cpp_vertex_shader_ptr const& vs )
	{
		return impl_->create_input_layout(elem_descs, elems_count, vs);
	}

	virtual buffer_ptr	create_buffer(size_t size)
	{
		return impl_->create_buffer(size);
	}

	virtual texture_ptr	create_tex2d(size_t width, size_t height, size_t num_samples, pixel_format fmt)
	{
		return impl_->create_tex2d(width, height, num_samples, fmt);
	}

	virtual texture_ptr	create_texcube(size_t width, size_t height, size_t num_samples, pixel_format fmt)
	{
		return impl_->create_texcube(width, height, num_samples, fmt);
	}

	virtual sampler_ptr	create_sampler(const sampler_desc& desc)
	{
		return impl_->create_sampler(desc);
	}

	virtual buffer_ptr get_index_buffer() const
	{
		EFLIB_ASSERT_UNIMPLEMENTED();
		return buffer_ptr();
	}

	virtual format get_index_format() const
	{
		EFLIB_ASSERT_UNIMPLEMENTED();
		return format_unknown;
	}

	virtual primitive_topology get_primitive_topology() const
	{
		EFLIB_ASSERT_UNIMPLEMENTED();
		return primitive_topology(0);
	}

	virtual bool get_render_target_available(render_target /*tar*/, size_t /*target_index*/) const
	{
		EFLIB_ASSERT_UNIMPLEMENTED();
		return false;
	}

	virtual framebuffer_ptr get_framebuffer() const
	{
		boost::mutex::scoped_lock locker(waiting_mutex_);
		
		boost::function<result()> cmd = boost::bind(&async_renderer::get_framebuffer_impl, this);
		cmds_.push_front(cmd);

		waiting_condition_.wait(locker);

		return current_frame_buffer_;
	}

	virtual pixel_format get_framebuffer_format(pixel_format /*pxfmt*/) const
	{
		EFLIB_ASSERT_UNIMPLEMENTED();
		return 0;
	}

	virtual eflib::rect<size_t> get_framebuffer_size() const
	{
		EFLIB_ASSERT_UNIMPLEMENTED();
		return eflib::rect<size_t>();
	}
	
	virtual boost::shared_ptr<shader_object> get_pixel_shader_code() const
	{
		return pixel_shader_code_;
	}

	virtual cpp_vertex_shader_ptr get_vertex_shader() const
	{
		return vertex_shader_;
	}

	virtual boost::shared_ptr<shader_object> get_vertex_shader_code() const
	{
		return vertex_shader_code_;
	}

	virtual raster_state_ptr get_rasterizer_state() const
	{
		EFLIB_ASSERT_UNIMPLEMENTED();
		return raster_state_ptr();
	}

	virtual cpp_pixel_shader_ptr get_pixel_shader() const
	{
		return pixel_shader_;
	}

	virtual cpp_blend_shader_ptr get_blend_shader() const
	{
		EFLIB_ASSERT_UNIMPLEMENTED();
		return cpp_blend_shader_ptr();
	}

	virtual viewport get_viewport() const
	{
		return viewport();
	}

	void run(renderer_parameters const* pparam, device_ptr const& hdev)
	{
		shared_impl_ = create_renderer_impl(pparam, hdev);
		impl_ = shared_impl_.get();

		rendering_thread_ = boost::thread( &async_renderer::working, this );
	}

private:
	virtual result set_vertex_buffer_impl(
		size_t starts_slot,
		vector<buffer_ptr> const& buffers,
		vector<size_t> const& strides,
		vector<size_t> const& offsets
		)
	{
		impl_->set_vertex_buffers(
			starts_slot, buffers.size(),
			boost::addressof(buffers[0]), boost::addressof(strides[0]), boost::addressof(offsets[0])
		);

		return result::ok;
	}

	virtual result set_vs_variable_value_impl( std::string const& name, vector<char> const& data )
	{
		impl_->set_vs_variable_value( name, static_cast<void const*>( boost::addressof(data[0]) ), data.size() );
		return result::ok;
	}

	virtual result set_vs_variable_pointer_impl( std::string const& name, vector<char> const& data )
	{
		impl_->set_vs_variable_pointer( name, static_cast<void const*>( boost::addressof(data[0]) ), data.size() );
		return result::ok;
	}

	virtual result set_ps_variable_impl( std::string const& name, vector<char> const& data )
	{
		impl_->set_ps_variable( name, boost::addressof(data[0]), data.size() );
		return result::ok;
	}

	virtual result present_impl()
	{
		boost::mutex::scoped_lock locker(waiting_mutex_);
		result ret = impl_->present();
		waiting_condition_.notify_one();
		return ret;
	}

	virtual result get_framebuffer_impl() const
	{
		boost::mutex::scoped_lock locker(waiting_mutex_);
		current_frame_buffer_ = impl_->get_framebuffer();
		waiting_condition_.notify_one();
		return result::ok;
	}

	virtual result flush_impl()
	{
		boost::mutex::scoped_lock locker(waiting_mutex_);
		result ret = impl_->flush();
		waiting_condition_.notify_one();
		return ret;
	}

	result release()
	{
		if ( rendering_thread_.joinable() )
		{
			cmds_.push_front( boost::bind(&async_renderer::exit_rendering_thread, this) );
			rendering_thread_.join();
		}
		return result::ok;
	}

	result exit_rendering_thread()
	{
		// Running in working thread.
		shared_impl_.reset();
		impl_ = NULL;
		return result::ok;
	}

	void working()
	{
		while( shared_impl_ )
		{
			boost::function<result()> cmd;
			cmds_.pop_back(&cmd);
			cmd();
		}
	}

	mutable eflib::bounded_buffer< boost::function<result()> > cmds_;
	
	boost::thread	rendering_thread_;
	renderer_ptr	shared_impl_;
	renderer*		impl_;

	mutable boost::condition	waiting_condition_;
	mutable boost::mutex		waiting_mutex_;

	// Cached states
	mutable framebuffer_ptr			current_frame_buffer_;
	boost::shared_ptr<shader_object>	vertex_shader_code_;
	boost::shared_ptr<shader_object>	pixel_shader_code_;
	cpp_vertex_shader_ptr					vertex_shader_;
	cpp_pixel_shader_ptr					pixel_shader_;
};

renderer_ptr create_async_renderer(renderer_parameters const* pparam, device_ptr const& hdev)
{
	boost::shared_ptr<async_renderer> ret( new async_renderer() );
	ret->run(pparam, hdev);
	return ret;
}

END_NS_SALVIAR();