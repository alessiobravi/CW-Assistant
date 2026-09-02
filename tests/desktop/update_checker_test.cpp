#include <QCoreApplication>
#include <QNetworkReply>

#include "updates/update_checker.hpp"

int main(int argc, char* argv[]) {
  QCoreApplication application(argc, argv);
  using cwassistant::desktop::update_detail::isTransientFailure;
  using cwassistant::desktop::update_detail::retryDelayMs;
  using cwassistant::desktop::update_detail::updateActionVisibility;

  if (!isTransientFailure(QNetworkReply::ContentNotFoundError, 404) ||
      !isTransientFailure(QNetworkReply::InternalServerError, 500) ||
      !isTransientFailure(QNetworkReply::TimeoutError, 0) ||
      isTransientFailure(QNetworkReply::AuthenticationRequiredError, 401) ||
      isTransientFailure(QNetworkReply::ProtocolInvalidOperationError, 400)) {
    return 1;
  }
  if (retryDelayMs(1) != 400 || retryDelayMs(2) != 1'200 ||
      retryDelayMs(3) != 2'500) {
    return 2;
  }

  const auto hidden = updateActionVisibility(false, true, false);
  const auto unsupported = updateActionVisibility(true, false, false);
  const auto downloadable = updateActionVisibility(true, true, false);
  const auto verified = updateActionVisibility(true, true, true);
  const auto retained_verified = updateActionVisibility(false, false, true);
  if (hidden.row || hidden.download || hidden.verified_download ||
      unsupported.row || unsupported.download ||
      unsupported.verified_download || !downloadable.row ||
      !downloadable.download || downloadable.verified_download ||
      !verified.row || verified.download || !verified.verified_download ||
      !retained_verified.row || retained_verified.download ||
      !retained_verified.verified_download) {
    return 3;
  }
  return 0;
}
