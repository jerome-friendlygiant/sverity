// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A run-time guard keeps both value arms in the model.
// expect: CDC-017 ERROR
module top(input logic clka, clkb, rst_n, inc, FRAME_FIFO, output logic [3:0] q);
  logic [3:0] bin, s1, s2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) bin <= '0;
    else if (inc) bin <= bin + 1;
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin s1 <= '0; s2 <= '0; end
    else begin
      if (FRAME_FIFO) s1 <= bin;
      else s1 <= '0;
      s2 <= s1;
    end
  assign q = s2;
endmodule
