// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The fan-in of q passes through an opaque function call. The rule decides on the model: the unreset register u is still in the cone, so the finding is reported.
// expect: RDC-010 WARNING
module top(input logic clka, rst_n, d, output logic q);
  logic u;
  function automatic logic pass(input logic a); return a; endfunction
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= pass(u);
  always_ff @(posedge clka) u <= d;
endmodule
