// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Each instance is analysed with its own elaborated FRAME_FIFO value.
// expect-count: CDC-017 ERROR 1
module pointer_path #(parameter FRAME_FIFO = 0)
  (input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  logic [3:0] bin, gray, s1, s2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) bin <= '0;
    else if (inc) bin <= bin + 1;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) gray <= '0;
    else gray <= bin ^ (bin >> 1);
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin s1 <= '0; s2 <= '0; end
    else begin
      if (FRAME_FIFO) s1 <= bin;
      else s1 <= gray;
      s2 <= s1;
    end
  assign q = s2;
endmodule

module top(input logic clka, clkb, rst_n, inc,
           output logic [3:0] q_inactive, q_active);
  pointer_path #(.FRAME_FIFO(0)) u_inactive(clka, clkb, rst_n, inc, q_inactive);
  pointer_path #(.FRAME_FIFO(1)) u_active(clka, clkb, rst_n, inc, q_active);
endmodule
