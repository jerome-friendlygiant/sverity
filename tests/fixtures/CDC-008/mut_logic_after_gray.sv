// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// MUTATION of good_gray_obligation.sv: logic (the increment mux) sits between the Gray flops and the crossing.
// expect: CDC-008 WARNING
module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  logic [3:0] bin, gray, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) gray <= '0; else gray <= bin ^ (bin >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, inc ? gray : 4'b0};
  assign q = s2;
endmodule
