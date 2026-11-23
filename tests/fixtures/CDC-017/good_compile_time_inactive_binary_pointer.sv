// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc -G FRAME_FIFO=0
// A compile-time inactive binary-pointer arm is absent from this configuration's CDC graph.
// expect-none: CDC-017
module top #(parameter FRAME_FIFO = 0)
  (input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  logic [3:0] bin, gray, temp, s1, s2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) bin <= '0;
    else if (inc) bin <= bin + 1;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) gray <= '0;
    else gray <= bin ^ (bin >> 1);
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin s1 <= '0; s2 <= '0; end
    else begin
      if (FRAME_FIFO) begin
        if (inc) begin temp = bin; s1 <= temp; end
      end
      else s1 <= gray;
      s2 <= s1;
    end
  assign q = s2;
endmodule
