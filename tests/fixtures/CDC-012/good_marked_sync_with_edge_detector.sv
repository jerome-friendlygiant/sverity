// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --profile fpga
// expect-none: CDC-012
module top(input logic clka, clkb, rst_n, d, output logic pulse);
  logic launch;
  (* ASYNC_REG = "TRUE" *) logic s1, s2;
  logic prior;
  always_ff @(posedge clka) launch <= d;
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin s1 <= 1'b0; s2 <= 1'b0; prior <= 1'b0; end
    else begin s1 <= launch; s2 <= s1; prior <= s2; end
  assign pulse = s2 ^ prior;
endmodule
