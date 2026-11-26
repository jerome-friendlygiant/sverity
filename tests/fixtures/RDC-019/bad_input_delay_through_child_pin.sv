// sverity-args: --top top --sdc @DIR@/input_delay_through_pin.sdc
// A set_false_path -through on the child input pin the reset enters covers the port: RDC-019 WARNING and RDC-003 WARNING fire (no RDC-003 INFO).
// expect: RDC-019 WARNING
// expect: RDC-003 WARNING
// expect-none: RDC-003 INFO
module c(input logic clk, rst_i, d, output logic qf, qa);
  always_ff @(posedge clk or posedge rst_i) if (rst_i) qf <= 1'b1; else qf <= 1'b0;
  always_ff @(posedge clk) if (rst_i) qa <= 1'b0; else qa <= d;
endmodule
module top(input logic clka, clkb, rsta, da, output logic qf, qa);
  c u (.clk(clka), .rst_i(rsta), .d(da), .qf, .qa);
endmodule
