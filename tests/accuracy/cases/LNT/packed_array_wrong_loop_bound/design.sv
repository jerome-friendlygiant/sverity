module lane_compactor #(
  parameter int LANES = 4,
  parameter int WORDS = 8
) (
  input  logic [WORDS-1:0] payload,
  output logic [LANES-1:0] lanes
);
  integer i;

  always_comb begin
    lanes = '0;
    for (i = 0; i < WORDS; i = i + 1) begin
      lanes[i] = payload[i];
    end
  end
endmodule

module top (
  input  logic [7:0] payload,
  output logic [3:0] lanes
);
  lane_compactor #(
    .LANES(4),
    .WORDS(8)
  ) u_compactor (
    .payload(payload),
    .lanes(lanes)
  );
endmodule
