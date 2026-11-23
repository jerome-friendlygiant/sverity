// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The runtime mux selects a synchronized level tap. CDC-044 currently reports
// conservative review evidence because it cannot prove this is not event use.
// expect: CDC-044 WARNING
module top(input logic clka, rst_n, rx, cfg_long, output logic lvl);
  logic s1, s2, s3;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) {s3, s2, s1} <= '0;
    else begin
    s1 <= rx;
    s2 <= s1;
    s3 <= s2;
  end
  assign lvl = cfg_long ? s3 : s2;
endmodule
