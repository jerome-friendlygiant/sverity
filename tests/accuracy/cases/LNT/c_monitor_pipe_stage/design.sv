// Valid/ready pipeline register. A checker watches for a payload change
// while the output is stalled; it is only present in simulation.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       in_valid,
  input  logic [7:0] in_data,
  output logic       in_ready,
  output logic       out_valid,
  output logic [7:0] out_data,
  input  logic       out_ready
);
  assign in_ready = !out_valid || out_ready;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      out_valid <= 1'b0;
      out_data  <= 8'd0;
    end else if (in_ready) begin
      out_valid <= in_valid;
      if (in_valid) out_data <= in_data;
    end
  end

  // synopsys translate_off
  logic [7:0] prev_data;
  logic       prev_stall;
  always @(posedge clk) begin
    prev_data  <= out_data;
    prev_stall <= out_valid && !out_ready;
    if (rst_n && prev_stall && out_data !== prev_data)
      $display("%m: payload changed during stall");
  end
  // synopsys translate_on
endmodule
