// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Near miss of bad_half_cycle.sv: q is clocked on posedge clka, the same edge as p, so no half-cycle path exists.
// expect-none: CDC-043
module top(input logic clka, rst_n, d, output logic q);
  logic p;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) p <= 1'b0; else p <= d;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= p;
endmodule
