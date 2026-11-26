// sverity-args: --top top --sdc @DIR@/good_replaced_clock_glob.sdc
// Clock 'old' declared by glob rst_* is replaced on the same elaborated source bit by 'current' declared with
// qualified spelling top.rst_n without -add: set_false_path -from [get_clocks old] covers nothing (round 4 A6).
// expect-none: RDC-027
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
