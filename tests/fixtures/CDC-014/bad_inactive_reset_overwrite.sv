// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-unresolved: CDC-014
// expect-none: CDC-014 INFO
// expect-none: CDC-024
module top(input logic clka, clkb, rst_n, output logic o);
  logic q, s1, s2, s3;
  always_ff @(posedge clka) q <= ~q;
  always_ff @(posedge clkb) begin
    s1 <= q;
    s2 <= s1;
    s3 <= s2;
  end
  always_ff @(posedge clkb or negedge rst_n) begin
    if (!rst_n) o <= 0;
    else begin
      o <= s2 != s3;
      if (rst_n) o <= 0;
    end
  end
endmodule
