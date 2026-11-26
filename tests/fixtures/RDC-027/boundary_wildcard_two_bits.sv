// sverity-args: --top top --sdc @DIR@/bits_wild.sdc
// One wildcard exception covers two independent reset bits: each bit is its own finding, so there are two.
// expect-count: RDC-027 WARNING 2
module top(input logic clka, d, input logic [5:2] rst_n, output logic q, p);
  always_ff @(posedge clka or negedge rst_n[3]) if (!rst_n[3]) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_n[4]) if (!rst_n[4]) p <= 1'b0; else p <= d;
endmodule
