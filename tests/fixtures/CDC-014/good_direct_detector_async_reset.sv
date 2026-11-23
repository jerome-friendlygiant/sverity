// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-014 INFO
// expect-none: CDC-014 WARNING
// expect: CDC-024 WARNING
module top(input logic clka, clkb, rst_n, output logic o);
  logic q, s1, s2, s3;
  always_ff @(posedge clka) q <= ~q;
  always_ff @(posedge clkb or negedge rst_n) begin
    if (!rst_n) begin
      s1 <= 0;
      s2 <= 0;
      s3 <= 0;
      o <= 0;
    end else begin
      s1 <= q;
      s2 <= s1;
      s3 <= s2;
      o <= s2 != s3;
    end
  end
endmodule
