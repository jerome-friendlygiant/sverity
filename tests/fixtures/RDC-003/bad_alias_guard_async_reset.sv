// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The reset is tested through a wire that copies it (`wire r = rst_n`): the copy is the same control, so rst_n is still the
// asynchronous reset of q and its release is not synchronised (RDC-003). Dropping the control hid it.
// expect: RDC-003 WARNING
// expect-none: RDC-019
module top(input logic clka, rst_n, d, output logic q);
  wire r = rst_n;
  always_ff @(posedge clka or negedge rst_n) if (!r) q <= 1'b0; else q <= d;
endmodule
