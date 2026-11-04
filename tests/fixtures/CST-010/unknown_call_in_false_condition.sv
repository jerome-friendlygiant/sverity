// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CST-010 WARNING
// A false condition does not stop the call in it from running, so its write stays visible.
module top(input logic clka, clkb, d, output logic q);
  logic src;
  always_ff @(posedge clka) src <= d;
  function automatic logic capture();
    q = src;
    return 1;
  endfunction
  always @(posedge clkb)
    if (capture() && 0) begin end
endmodule
