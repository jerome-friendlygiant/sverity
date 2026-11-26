// sverity-args: --top top --sdc @DIR@/good_replaced_clock_exception.sdc
// The exception names clock old, which the later create_clock on the same port replaces (no -add): old is no longer in force, so
// it covers nothing and RDC-027 must stay silent.
// expect-none: RDC-027
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 0; else q <= d;
endmodule
