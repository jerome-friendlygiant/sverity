// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The opposite-edge destination is reached through one same-domain register, the half-cycle boundary case.
// expect: CDC-043 WARNING
// expect-confidence: CDC-043 heuristic
module top(input logic clka, rst_n, d, output logic q);
  logic p;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) p <= 1'b0; else p <= d;
  always_ff @(negedge clka or negedge rst_n)
    if (!rst_n) q <= 1'b0; else q <= p;
endmodule
