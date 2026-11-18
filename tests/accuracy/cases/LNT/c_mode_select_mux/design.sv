// Output mode multiplexer: four modes select among sample, hold, ramp and
// test pattern sources. All four mode values are listed.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [1:0] mode,
  input  logic [7:0] sample,
  output logic [7:0] out_q
);
  logic [7:0] hold_q;
  logic [7:0] ramp_q;
  logic [7:0] out_c;

  always_comb begin
    case (mode) // synopsys full_case parallel_case
      2'b00:   out_c = sample;
      2'b01:   out_c = hold_q;
      2'b10:   out_c = ramp_q;
      2'b11:   out_c = 8'hA5;
    endcase
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      hold_q <= 8'd0;
      ramp_q <= 8'd0;
      out_q  <= 8'd0;
    end else begin
      if (mode == 2'b00) hold_q <= sample;
      ramp_q <= ramp_q + 8'd1;
      out_q  <= out_c;
    end
  end
endmodule
