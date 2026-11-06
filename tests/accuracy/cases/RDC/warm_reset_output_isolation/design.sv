module top (
  input logic clk,
  input logic warm_reset_n,
  input logic isolate,
  input logic send,
  input logic [7:0] payload,
  output logic [7:0] accepted_data
);
  logic [7:0] outgoing;
  logic [7:0] isolated_data;
  // The power controller owns isolation around warm restart.
  always_ff @(posedge clk or negedge warm_reset_n) begin
    if (!warm_reset_n)
      outgoing <= '0;
    else if (send)
      outgoing <= payload;
  end
  assign isolated_data = isolate ? 8'h00 : outgoing;
  always_ff @(posedge clk)
    accepted_data <= isolated_data;
endmodule
