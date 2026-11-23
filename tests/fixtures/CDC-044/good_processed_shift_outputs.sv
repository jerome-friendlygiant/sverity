// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-none: CDC-044
module top(input logic clka, rst_n, rx, sel, output logic [1:0] masked, muxed, inverted);
  logic s1, s2, s3, s4;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) {s4, s3, s2, s1} <= '0;
    else {s4, s3, s2, s1} <= {s3, s2, s1, rx};
  assign masked = {s4 & 1'b1, s3 | 1'b0};
  assign muxed = sel ? {s4, s3} : 2'b00;
  assign inverted = {~s4, ~s3};
endmodule
