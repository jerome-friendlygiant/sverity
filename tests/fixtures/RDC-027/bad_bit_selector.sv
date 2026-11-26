// sverity-args: --top top --sdc @DIR@/bit.sdc
// The exception names the port bit rst_n[1] only: it covers that bit's reset, not the other bit rst_n[0] (one finding).
// expect-count: RDC-027 WARNING 1
module top(input logic clka, input logic [1:0] rst_n, d, output logic q0, q1);
  always_ff @(posedge clka or negedge rst_n[0]) if (!rst_n[0]) q0 <= 1'b0; else q0 <= d;
  always_ff @(posedge clka or negedge rst_n[1]) if (!rst_n[1]) q1 <= 1'b0; else q1 <= d;
endmodule
