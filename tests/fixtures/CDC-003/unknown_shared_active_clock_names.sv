// sverity-args: --top top --sdc @DIR@/shared_active_names.sdc
// The SDC object named 'shared' applies to two distinct RTL clock roots, while clka also has another active name.
// expect: CDC-003 WARNING
// expect-unresolved: CDC-003
// expect-coverage: constraints INCOMPLETE
module top(input logic clka, clkb, d, output logic q_src, q_dst);
  always_ff @(posedge clka) q_src <= d;
  always_ff @(posedge clkb) q_dst <= q_src;
endmodule
