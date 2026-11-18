// Proportional term of a control loop: term = (error * KP) / 8.
// The error is a signed 16-bit value from the subtractor stage; the result
// is registered and held until the next valid error arrives.
module top (
  input  logic               clk,
  input  logic               rst_n,
  input  logic               err_valid,
  input  logic signed [15:0] err_s,
  output logic signed [15:0] term,
  output logic               term_valid
);
  localparam logic [15:0] KP = 16'd5;

  logic signed [15:0] term_q;
  logic               valid_q;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      term_q  <= 16'sd0;
      valid_q <= 1'b0;
    end else begin
      valid_q <= err_valid;
      if (err_valid) term_q <= (err_s * KP) >>> 3;
    end
  end

  assign term       = term_q;
  assign term_valid = valid_q;
endmodule
