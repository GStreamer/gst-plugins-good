bin_SCRIPTS = \
	gst-launch-ext-@GST_MAJORMINOR@ \
	gst-visualise-@GST_MAJORMINOR@

man_MANS = \
	gst-launch-ext-@GST_MAJORMINOR@.1 \
	gst-visualise-@GST_MAJORMINOR@.1

CLEANFILES = $(man_MANS)

EXTRA_DIST = \
	gst-launch-ext-0.6 gst-visualise-0.6 \
	gst-launch-ext.1.in gst-visualise.1.in

%-@GST_MAJORMINOR@.1: %.1.in
	sed \
                -e s,gst-complete,gst-complete-@GST_MAJORMINOR@,g \
                -e s,gst-compprep,gst-compprep-@GST_MAJORMINOR@,g \
                -e s,gst-feedback,gst-feedback-@GST_MAJORMINOR@,g \
                -e s,gst-inspect,gst-inspect-@GST_MAJORMINOR@,g \
                -e s,gst-launch,gst-launch-@GST_MAJORMINOR@,g \
                -e s,gst-launch-ext,gst-launch-ext-@GST_MAJORMINOR@,g \
                -e s,gst-md5sum,gst-md5sum-@GST_MAJORMINOR@,g \
                -e s,gst-register,gst-register-@GST_MAJORMINOR@,g \
                -e s,gst-typefind,gst-typefind-@GST_MAJORMINOR@,g \
                -e s,gst-visualise,gst-visualise-@GST_MAJORMINOR@,g \
                -e s,gst-xmllaunch,gst-xmllaunch-@GST_MAJORMINOR@,g \
                $< >$@

