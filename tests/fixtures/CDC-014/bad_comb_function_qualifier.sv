// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-014 WARNING
// expect-unresolved: CDC-014
// expect-none: CDC-014 INFO
// expect-none: CDC-024
module top(input logic clka, clkb, output logic o);
  logic q, s1, s2, s3;
  function automatic logic f();
    return s2 == s3;
  endfunction
  always_ff @(posedge clka) q <= ~q;
  always_ff @(posedge clkb) begin
    s1 <= q;
    s2 <= s1;
    s3 <= s2;
  end
  assign o = f() && (s2 != s3);
endmodule
