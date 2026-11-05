// sverity-args: --top top --sdc @DIR@/unknown_undecidable_if_hides_no_crossing.sdc
// expect: CST-018 WARNING
// expect: CDC-003 WARNING
// expect-none: CDC-003 ERROR
module top(input logic clka, clkb, rst_n, data, output logic q);
  logic launch;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) launch <= 1'b0; else launch <= data;
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= 1'b0; else q <= launch;
endmodule
