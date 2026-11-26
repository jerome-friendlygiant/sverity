// sverity-args: --top top --sdc @DIR@/good_replaced_clock_bus_bit.sdc
// create_clock on rst_n[3] without -add replaces clock 'initial' on bit 3 only; bit 4 still carries 'initial', so
// set_false_path -from [get_clocks initial] covers only rst_n[4] (1 finding, round 4 A6).
// expect-count: RDC-027 WARNING 1
module top(input logic clka, d, input logic [2:5] rst_n, output logic q, p);
  always_ff @(posedge clka or negedge rst_n[3]) if (!rst_n[3]) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_n[4]) if (!rst_n[4]) p <= 1'b0; else p <= d;
endmodule
