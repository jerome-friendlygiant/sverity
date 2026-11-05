// sverity-args: --top top --sdc @DIR@/two_clocks.sdc
// r is written on clocks a and b and captured on a. One writer is asynchronous to the capture: order must not hide it.
// expect: CDC-003 ERROR
module top(input logic a, b, d, output logic q);
  logic r;
  always @(posedge a) r <= d;
  always @(posedge b) r <= ~d;
  always_ff @(posedge a) q <= r;
endmodule
