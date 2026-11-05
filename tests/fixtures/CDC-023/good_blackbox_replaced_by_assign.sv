// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Near miss of bad_blackbox_capture.sv: the unmodelled vendor_ip instance is replaced by a plain assign, so no black box drives the captured net.
// expect-none: CDC-023
module top(input logic clka, clkb, d, output logic q);
  logic o;
  assign o = d;
  always_ff @(posedge clkb) q <= o;
endmodule
