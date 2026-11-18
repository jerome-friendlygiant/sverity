// Tick counter with an enable and a freeze input from the debug block.
// While frozen the count holds its value.
module top (
  input  logic        clk,
  input  logic        rst_n,
  input  logic        enable,
  input  logic        freeze,
  output logic [15:0] cnt_o
);
  logic [15:0] cnt;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) cnt <= 16'd0;
    else if (enable
// synopsys translate_off
             && !freeze
// synopsys translate_on
            ) cnt <= cnt + 16'd1;
  end

  assign cnt_o = cnt;
endmodule
