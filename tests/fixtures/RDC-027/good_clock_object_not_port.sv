// sverity-args: --top top --sdc @DIR@/clock_obj.sdc
// get_clocks rst_n selects the clock object created on data input d, not the reset port rst_n: no exception covers the reset.
// expect-none: RDC-027
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
