// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Reset is driven directly by its reset source; no data-leaf select/gate is present.
// expect-none: RDC-017
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
